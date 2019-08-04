#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <libdrm/drm_fourcc.h>

#include <cairo.h>
#include <cairo-gl.h>

extern "C" {
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/joystick.h>
#include <linux/videodev2.h>
}

using namespace std::string_literals;
using namespace std::string_view_literals;

static constexpr std::uint32_t num_buffers = 8;

static constexpr auto vertex_shader_src = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main()
{
   gl_Position = a_position;
   v_texCoord = a_texCoord;
}
)";

static constexpr auto fragment_shader_src = R"(
#extension GL_OES_EGL_image_external: require
precision mediump float;
varying vec2 v_texCoord;
uniform samplerExternalOES s_texture;
void main()
{
  gl_FragColor = texture2D(s_texture, v_texCoord);
}
)";

static ::PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
static ::PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
static ::PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

template <class T>
static inline T get_egl_proc(const char* proc_name) {
  return reinterpret_cast<T>(::eglGetProcAddress(proc_name));
}

class fractal_controller;

struct window_context {
  int width;
  int height;

  ::wl_display* display;
  ::wl_compositor* compositor;
  ::wl_subcompositor* subcompositor;
  ::wl_shell* shell;
  ::wl_shell_surface* shell_surface;
  ::wl_callback* redraw_cb;

  ::EGLDisplay egl_display;
  ::EGLConfig egl_config;
  ::EGLContext egl_context;

  struct surface {
    ::wl_surface* surface;
    ::wl_subsurface* subsurface;
    ::wl_egl_window* egl_window;
    ::EGLSurface egl_surface;

    ::cairo_surface_t* cairo_surface;
  };
  surface main_surface;
  surface overlay_surface;

  struct {
    ::GLuint program;
    ::GLuint a_position;
    ::GLuint a_tex_coord;
    ::GLuint s_texture;
    ::GLuint textures[num_buffers];
  } texture;

  ::cairo_device_t* cairo_device;

  int video_fd;
  struct buffer_context {
    std::uint8_t* ptr;
    std::uint32_t length;
    std::uint32_t offset;
    int fd;
  };
  std::array<buffer_context, num_buffers> video_buffers;
  std::optional<std::uint32_t> processing_buffer_index;
  std::optional<std::uint32_t> displaying_buffer_index;

  bool running;
  int epoll_fd;
  int display_fd;
  int timer_fd;
  int joystick_fd;

  struct app_state {
    std::uint64_t animation_frame;
    double cr, ci, scale, offset_x, offset_y;
  } app;

  struct {
    std::uint8_t num_axes;
    std::uint8_t num_buttons;
    std::unique_ptr<std::int16_t[]> axes;
    std::unique_ptr<std::int16_t[]> buttons;
  } joystick;

  std::unique_ptr<fractal_controller> fractal_ctl;
};

static inline void perror_exit(const char* str) {
  std::perror(str);
  std::exit(EXIT_FAILURE);
}

static ::GLuint load_shader(::GLenum type, const char* shader_src) {
  ::GLuint shader = ::glCreateShader(type);
  if (!shader) {
    return 0;
  }

  ::glShaderSource(shader, 1, &shader_src, nullptr);
  ::glCompileShader(shader);

  ::GLint compiled;
  ::glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    ::glDeleteShader(shader);
    return 0;
  }

  return shader;
}

static ::GLuint create_gl_program(const char* vshader_src, const char* fshader_src) {
  ::GLuint vshader = load_shader(GL_VERTEX_SHADER, vshader_src);
  if (!vshader) {
    std::cerr << "create_gl_program: failed to load vertex shader" << std::endl;
    return 0;
  }

  ::GLuint fshader = load_shader(GL_FRAGMENT_SHADER, fshader_src);
  if (!fshader) {
    std::cerr << "create_gl_program: failed to load fragment shader" << std::endl;
    ::glDeleteShader(vshader);
    return 0;
  }

  ::GLuint program = ::glCreateProgram();
  if (!program) {
    std::cerr << "create_gl_program: failed to create glprogram" << std::endl;
    ::glDeleteShader(vshader);
    ::glDeleteShader(fshader);
    return 0;
  }

  ::glAttachShader(program, vshader);
  ::glAttachShader(program, fshader);
  ::glLinkProgram(program);

  ::GLint linked{};
  ::glGetProgramiv(program, GL_LINK_STATUS, &linked);

  ::glDeleteShader(vshader);
  ::glDeleteShader(fshader);

  if (!linked) {
    std::cerr << "create_gl_program: failed to link glprogram" << std::endl;
    ::glDeleteProgram(program);
    return 0;
  }

  return program;
}

template <std::size_t IntegerWidth>
class fix {
  std::uint32_t value_;

public:
  static constexpr std::size_t value_width = sizeof(std::uint32_t) * 8;
  static constexpr std::size_t integer_width = IntegerWidth;
  static constexpr std::size_t fractional_width = value_width - integer_width;

  static constexpr std::uint32_t fractional_mask = (1ul << fractional_width) - 1;
  static constexpr std::uint32_t intreger_mask = ~fractional_mask;

  static_assert(integer_width < value_width);

  explicit fix(std::uint32_t v) : value_{v} {};

  explicit fix(double v) {
    value_ = double_to_fix(v);
  }

  static std::int32_t double_to_fix(double v) {
    union {
      double d;
      std::uint64_t u;
      struct {
        std::uint64_t frac : 52;
        std::uint64_t exp : 11;
        std::uint64_t sign : 1;
      } s;
    } df;
    df.d = v;

    if ((df.u & 0x7ffffffffffffffful) == 0) {
      return 0;
    }

    auto frac = static_cast<std::int64_t>(df.s.frac) | (1ul << 52);
    auto exp = static_cast<int16_t>(df.s.exp) - 1023;
    bool sign = df.s.sign;
    if (sign) frac = -frac;

    int ap_w2 = 52 + 2;
    int ap_i2 = exp + 2;
    constexpr int ap_f = static_cast<int>(fractional_width);
    int f2 = ap_w2 - ap_i2;
    int shift = f2 > ap_f ? f2 - ap_f : ap_f - f2;

    if (f2 == ap_f) {
      return frac;
    } else if (f2 > ap_f) {
      if (shift < ap_w2) {
        return frac >> shift;
      } else {
        return df.s.sign ? -1 : 0;
      }
    } else {
      if (shift < static_cast<int>(value_width)) {
        return frac << shift;
      } else {
        return 0;
      }
    }
  }

  inline std::uint32_t value() const {
    return value_;
  }

  inline double to_double() const {
    if (!value_) {
      return 0.0;
    }
    return static_cast<double>(static_cast<int32_t>(value_)) / (1ul << fractional_width);
  }

  inline std::int32_t integer() const {
    return static_cast<int32_t>(value_) / (1ul << fractional_width);
  }

  template <std::size_t Digits>
  inline std::uint64_t fractional() const {
    static_assert(Digits > 0);
    constexpr auto d = pow10(Digits);
    const auto f = static_cast<std::uint64_t>(value_ & fractional_mask);
    return f * d / (1ul << fractional_width);
  }

private:
  static constexpr std::uint64_t pow10(std::size_t n) {
    std::uint64_t v = 1;
    for (auto i = 0ul; i < n; ++i) v *= 10;
    return v;
  }
};

class fractal_controller {
  int fd_;
  std::size_t size_;
  std::uint64_t* reg_;

public:
  fractal_controller(std::uintptr_t base_addr)
      : fd_{-1}, reg_{static_cast<std::uint64_t*>(MAP_FAILED)} {
    fd_ = ::open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_ < 0) {
      throw std::runtime_error{"failed to open /dev/mem: "s + std::strerror(errno)};
    }

    const auto s = ::sysconf(_SC_PAGESIZE);
    if (s < 0) {
      throw std::runtime_error{"failed to get page size: "s + std::strerror(errno)};
    }
    size_ = static_cast<std::size_t>(s);

    reg_ = static_cast<std::uint64_t*>(
        ::mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, base_addr));
    if (reg_ == MAP_FAILED) {
      throw std::runtime_error{"mmap: "s + std::strerror(errno)};
    }
  }

  ~fractal_controller() {
    if (fd_ > 0) {
      ::close(fd_);
    }

    if (reg_ != MAP_FAILED) {
      ::munmap(reg_, size_);
    }
  }

#define FRACTAL_CONTROLLER_GETTER_SETTER(name, index)       \
  fix<4> name() const {                                     \
    return fix<4>{static_cast<std::uint32_t>(reg_[index])}; \
  }                                                         \
  void set_##name(double name) {                            \
    reg_[index] = fix<4>::double_to_fix(name);              \
  }

  FRACTAL_CONTROLLER_GETTER_SETTER(x0, 2u)
  FRACTAL_CONTROLLER_GETTER_SETTER(y0, 3u)
  FRACTAL_CONTROLLER_GETTER_SETTER(dx, 4u)
  FRACTAL_CONTROLLER_GETTER_SETTER(dy, 5u)
  FRACTAL_CONTROLLER_GETTER_SETTER(cr, 6u)
  FRACTAL_CONTROLLER_GETTER_SETTER(ci, 7u)

#undef FRACTAL_CONTROLLER_GETTER_SETTER
};

std::tuple<::EGLDisplay, ::EGLConfig, ::EGLContext> init_egl(::wl_display* native_display) {
  // clang-format off
  static const ::EGLint config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RED_SIZE,        1,
    EGL_GREEN_SIZE,      1,
    EGL_BLUE_SIZE,       1,
    EGL_ALPHA_SIZE,      1,
    EGL_DEPTH_SIZE,      1,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE,
  };

  static const ::EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE,
  };
  // clang-format on

  ::EGLDisplay display = ::eglGetDisplay(static_cast<::EGLNativeDisplayType>(native_display));
  if (!display) {
    throw std::runtime_error{"failed to create egl display"};
  }

  if (::eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
    throw std::runtime_error{"failed to initialize egl display"};
  }

  if (!::eglBindAPI(EGL_OPENGL_ES_API)) {
    throw std::runtime_error{"failed to bind EGL client API"};
  }

  ::EGLConfig config{};
  ::EGLint num_configs{};
  ::eglChooseConfig(display, config_attribs, &config, 1, &num_configs);
  if (!num_configs) {
    throw std::runtime_error{"failed to get EGL config"};
  }

  ::EGLContext context = ::eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);

  return std::make_tuple(display, config, context);
}

window_context::surface make_surface(
    ::wl_compositor* compositor,
    std::tuple<const ::EGLDisplay&, const ::EGLConfig&, const ::EGLContext&> egl, int width,
    int height) {
  window_context::surface surface;
  surface.subsurface = nullptr;
  surface.cairo_surface = nullptr;

  surface.surface = ::wl_compositor_create_surface(compositor);
  if (!surface.surface) {
    throw std::runtime_error{"failed to create surface"};
  }

  surface.egl_window = wl_egl_window_create(surface.surface, width, height);
  if (!surface.egl_window) {
    throw std::runtime_error{"failed to create egl window"};
  }

  const auto& [display, config, context] = egl;
  surface.egl_surface = ::eglCreateWindowSurface(
      display, config, static_cast<::EGLNativeWindowType>(surface.egl_window), nullptr);
  if (surface.egl_surface == EGL_NO_SURFACE) {
    throw std::runtime_error{"failed to create egl surface"};
  }

  return surface;
}

window_context::surface make_subsurface(
    ::wl_compositor* compositor, ::wl_subcompositor* subcompositor,
    std::tuple<const ::EGLDisplay&, const ::EGLConfig&, const ::EGLContext&> egl,
    const window_context::surface& parent, int width, int height) {
  auto surface = make_surface(compositor, egl, width, height);

  surface.subsurface =
      ::wl_subcompositor_get_subsurface(subcompositor, surface.surface, parent.surface);

  return surface;
}

void shell_surface_handle_ping([[maybe_unused]] void* data, ::wl_shell_surface* shell_surface,
                               std::uint32_t serial) {
  ::wl_shell_surface_pong(shell_surface, serial);
}

void shell_surface_handle_configure([[maybe_unused]] void* data,
                                    [[maybe_unused]] ::wl_shell_surface* shell_surface,
                                    [[maybe_unused]] std::uint32_t edges,
                                    [[maybe_unused]] std::int32_t width,
                                    [[maybe_unused]] std::int32_t height) {}

void shell_surface_handle_popup_done([[maybe_unused]] void* data,
                                     [[maybe_unused]] ::wl_shell_surface* shell_surface) {}

const struct wl_shell_surface_listener shell_surface_listener = {
    shell_surface_handle_ping,
    shell_surface_handle_configure,
    shell_surface_handle_popup_done,
};

static void registry_handle_global(void* data, ::wl_registry* registry, std::uint32_t name,
                                   const char* interface, [[maybe_unused]] std::uint32_t version) {
  auto ctx = static_cast<::window_context*>(data);

  const auto ifname = std::string_view{interface};
  if (ifname == "wl_compositor"sv) {
    auto compositor = ::wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    ctx->compositor = static_cast<::wl_compositor*>(compositor);
  } else if (ifname == "wl_subcompositor"sv) {
    auto subcompositor = ::wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    ctx->subcompositor = static_cast<::wl_subcompositor*>(subcompositor);
  } else if (ifname == "wl_shell"sv) {
    auto shell = ::wl_registry_bind(registry, name, &wl_shell_interface, 1);
    ctx->shell = static_cast<::wl_shell*>(shell);
  }
}

static void registry_handle_global_remove([[maybe_unused]] void* data,
                                          [[maybe_unused]] ::wl_registry* registry,
                                          [[maybe_unused]] uint32_t name) {}

static const ::wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove,
};

static void redraw_main_surface(::window_context* ctx, [[maybe_unused]] std::uint32_t time) {
  if (!::eglMakeCurrent(ctx->egl_display, ctx->main_surface.egl_surface,
                        ctx->main_surface.egl_surface, ctx->egl_context)) {
    std::cerr << "eglMakeCurrent(main_surface) failed" << std::endl;
    return;
  }

  ::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  ::glClear(GL_COLOR_BUFFER_BIT);

  if (ctx->displaying_buffer_index) {
    // clang-format off
    static constexpr GLfloat tex_pos[] = {
        -1.0f, 1.0f,  0.0f,
        -1.0f, -1.0f, 0.0f,
        1.0f,  -1.0f, 0.0f,
        1.0f,  1.0f,  0.0f,
    };

    static constexpr GLfloat tex_coord[] = {
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
        1.0f, 0.0f,
    };

    static constexpr GLushort indices[] = {
      0, 1, 2,
      0, 2, 3,
    };
    // clang-format on

    const auto& t = ctx->texture;

    ::glUseProgram(t.program);

    ::glActiveTexture(GL_TEXTURE0);
    ::glBindTexture(GL_TEXTURE_EXTERNAL_OES, t.textures[ctx->displaying_buffer_index.value()]);

    ::glVertexAttribPointer(t.a_position, 3, GL_FLOAT, GL_FALSE, 0, tex_pos);
    ::glVertexAttribPointer(t.a_tex_coord, 2, GL_FLOAT, GL_FALSE, 0, tex_coord);

    ::glEnableVertexAttribArray(t.a_position);
    ::glEnableVertexAttribArray(t.a_tex_coord);

    ::glUniform1i(t.s_texture, 0);
    ::glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    ::glDisableVertexAttribArray(t.a_position);
    ::glDisableVertexAttribArray(t.a_tex_coord);
    ::glUseProgram(0);
  }
}

static void flush_main_surface(::window_context* ctx, [[maybe_unused]] std::uint32_t time) {
  if (!::eglMakeCurrent(ctx->egl_display, ctx->main_surface.egl_surface,
                        ctx->main_surface.egl_surface, ctx->egl_context)) {
    std::cerr << "eglMakeCurrent(main_surface) failed" << std::endl;
    return;
  }

  ::eglSwapBuffers(ctx->egl_display, ctx->main_surface.egl_surface);
  ::glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
}

static void redraw_overlay_surface(::window_context* ctx, [[maybe_unused]] std::uint32_t time) {
  if (!::eglMakeCurrent(ctx->egl_display, ctx->overlay_surface.egl_surface,
                        ctx->overlay_surface.egl_surface, ctx->egl_context)) {
    std::cerr << "eglMakeCurrent(overlay_surface) failed" << std::endl;
    return;
  }

  const auto width = ctx->width;
  const auto height = ctx->height;

  if (!ctx->overlay_surface.cairo_surface) {
    ctx->overlay_surface.cairo_surface = ::cairo_gl_surface_create_for_egl(
        ctx->cairo_device, ctx->overlay_surface.egl_surface, width, height);
  }
  auto cr = ::cairo_create(ctx->overlay_surface.cairo_surface);

  ::cairo_set_source_rgba(cr, 0.125, 0.125, 0.125, 0.75);
  ::cairo_rectangle(cr, 31.5, 63.5, 437, 153);
  ::cairo_fill_preserve(cr);

  ::cairo_set_line_width(cr, 1.0);
  ::cairo_set_source_rgba(cr, 0, 0, 0, 1.0);
  ::cairo_stroke(cr);

  ::cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  ::cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

  ::cairo_set_font_size(cr, 28);
  ::cairo_move_to(cr, 48.0, 104.0);
  ::cairo_show_text(cr, "Julia Set Explorer");

  ::cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  ::cairo_set_font_size(cr, 14);
  ::cairo_move_to(cr, 364.0, 104.0);
  ::cairo_show_text(cr, "by @myon___");

  constexpr auto max_len = 255;
  char str[max_len] = {};
  std::snprintf(str, max_len,
                "c: %12.8f%+.8fi\n"
                "x: %12.8f,  y:  %12.8f,  scale: %12.8f\n"
                "\n"
                "fps (fpga / display): %.4f / %.4f",
                ctx->app.cr, ctx->app.ci, ctx->app.offset_x, ctx->app.offset_y, ctx->app.scale,
                0.0000, 0.0000);

  ::cairo_set_font_size(cr, 12);
  for (auto [y, p] = std::tuple{0, std::begin(str)}; p < std::end(str) && *p;) {
    auto q = p;
    for (; q < std::end(str) && *q != '\n' && *q != '\0'; ++q)
      ;
    *q = '\0';
    ::cairo_move_to(cr, 48.0, 134.0 + 20 * y++);
    ::cairo_show_text(cr, p);
    p = q + 1;
  }

  ::cairo_destroy(cr);
}

static void flush_overlay_surface(::window_context* ctx, [[maybe_unused]] std::uint32_t time) {
  if (!::eglMakeCurrent(ctx->egl_display, ctx->overlay_surface.egl_surface,
                        ctx->overlay_surface.egl_surface, ctx->egl_context)) {
    std::cerr << "eglMakeCurrent(overlay_surface) failed" << std::endl;
    return;
  }

  ::cairo_gl_surface_swapbuffers(ctx->overlay_surface.cairo_surface);
}

static void redraw(void* data, ::wl_callback* callback, std::uint32_t time);

static const ::wl_callback_listener redraw_listener = {
    redraw,
};

static void redraw(void* data, ::wl_callback* callback, [[maybe_unused]] std::uint32_t time) {
  auto ctx = static_cast<::window_context*>(data);

  if (callback) {
    ::wl_callback_destroy(callback);
  }

  redraw_main_surface(ctx, time);
  redraw_overlay_surface(ctx, time);

  flush_overlay_surface(ctx, time);
  flush_main_surface(ctx, time);

  ctx->redraw_cb = ::wl_surface_frame(ctx->main_surface.surface);
  ::wl_callback_add_listener(ctx->redraw_cb, &redraw_listener, ctx);
}

static void handle_display_events(window_context* ctx, std::uint32_t events) {
  if (events & EPOLLERR || events & EPOLLHUP) {
    ctx->running = false;
    return;
  }

  if (events & EPOLLIN) {
    if (::wl_display_dispatch(ctx->display) == -1) {
      ctx->running = false;
      return;
    }
  }

  if (events & EPOLLOUT) {
    int ret = ::wl_display_flush(ctx->display);
    if (ret == 0) {
      ::epoll_event ep{};
      ep.events = EPOLLIN | EPOLLERR | EPOLLHUP;
      ep.data.ptr = ctx;
      ::epoll_ctl(ctx->epoll_fd, EPOLL_CTL_MOD, ctx->display_fd, &ep);
    } else if (ret == -1 && errno != EAGAIN) {
      ctx->running = false;
      return;
    }
  }
}

static void handle_v4l2_events(window_context* ctx, std::uint32_t events) {
  if (events & EPOLLERR || events & EPOLLHUP) {
    ctx->running = false;
    return;
  }

  if (!(events & EPOLLIN)) {
    return;
  }

  std::uint32_t new_index{};
  {
    ::v4l2_plane planes[VIDEO_MAX_PLANES];
    ::v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;
    if (::ioctl(ctx->video_fd, VIDIOC_DQBUF, &buf) == -1) {
      std::cerr << "VIDIOC_DQBUF: " << std::strerror(errno) << std::endl;
      ctx->running = false;
      return;
    }
    new_index = buf.index;
  }

  if (ctx->displaying_buffer_index) {
    ::v4l2_plane planes[VIDEO_MAX_PLANES];
    ::v4l2_buffer buf{};
    buf.index = ctx->displaying_buffer_index.value();
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;
    if (::ioctl(ctx->video_fd, VIDIOC_QUERYBUF, &buf) == -1) {
      std::cerr << "VIDIOC_QUERYBUF: " << std::strerror(errno) << std::endl;
      ctx->running = false;
      return;
    }
    if (::ioctl(ctx->video_fd, VIDIOC_QBUF, &buf) == -1) {
      std::cerr << "VIDIOC_QBUF: " << std::strerror(errno) << std::endl;
      ctx->running = false;
      return;
    }
  }

  ctx->displaying_buffer_index = ctx->processing_buffer_index;
  ctx->processing_buffer_index = new_index;
}

static void handle_timer_events(window_context* ctx, std::uint32_t events) {
  if (events & EPOLLERR || events & EPOLLHUP) {
    ctx->running = false;
    return;
  }

  if (events & EPOLLIN) {
    std::uint64_t exp{};
    if (::read(ctx->timer_fd, &exp, sizeof exp) != sizeof exp) {
      std::cerr << "timer_fd read: " << std::strerror(errno) << std::endl;
      ctx->running = false;
      return;
    }

    if (ctx->joystick_fd >= 0) {
      const auto& j = ctx->joystick;
      auto& s = ctx->app;

      if (j.buttons[1] && s.scale <= 100) s.scale += 0.001;
      if (j.buttons[2] && s.scale >= 0.0) s.scale -= 0.001;

      if (j.axes[4] > 0) s.offset_x += 0.001;
      if (j.axes[4] < 0) s.offset_x -= 0.001;

      if (j.axes[5] < 0) s.offset_y += 0.001;
      if (j.axes[5] > 0) s.offset_y -= 0.001;
    }

    {
      constexpr auto ratio = 1080.0 / 1920.0;
      const auto cr = ctx->app.cr;
      const auto ci = ctx->app.ci;
      const auto scale = ctx->app.scale;
      const auto x1 = 1.0 * scale;
      const auto y1 = ratio * scale;
      const auto dx = 2.0 * x1 / 1920.0;
      const auto dy = 2.0 * y1 / 1080.0;
      const auto offset_x = ctx->app.offset_x;
      const auto offset_y = ctx->app.offset_y;
      const auto x0 = x1 - offset_x;
      const auto y0 = y1 + offset_y;

      ctx->fractal_ctl->set_cr(cr);
      ctx->fractal_ctl->set_ci(ci);
      ctx->fractal_ctl->set_x0(x0);
      ctx->fractal_ctl->set_y0(y0);
      ctx->fractal_ctl->set_dx(dx);
      ctx->fractal_ctl->set_dy(dy);
    }

    /*
    auto i = ctx->app.animation_frame + exp;
    if (i >= 9000) i = 1000;

    const auto t = (static_cast<double>(i) / 10000) * 6.28;
    ctx->fractal_ctl->set_cr(0.7885 * std::cos(t));
    ctx->fractal_ctl->set_ci(0.7885 * std::sin(t));

    ctx->app.animation_frame = i;
    */

    return;
  }
}

static void handle_joystick_events(window_context* ctx, std::uint32_t events) {
  if (events & EPOLLERR || events & EPOLLHUP) {
    ctx->running = false;
    return;
  }

  if (events & EPOLLIN) {
    ::js_event jse{};
    if (::read(ctx->joystick_fd, &jse, sizeof jse) != sizeof jse) {
      std::cerr << "joystick_fd read: " << std::strerror(errno) << std::endl;
      ctx->running = false;
      return;
    }

    switch (jse.type & ~JS_EVENT_INIT) {
      case JS_EVENT_AXIS:
        if (jse.number < ctx->joystick.num_axes) {
          ctx->joystick.axes[jse.number] = jse.value;
        }
        break;
      case JS_EVENT_BUTTON:
        if (jse.number < ctx->joystick.num_buttons) {
          ctx->joystick.buttons[jse.number] = jse.value;
        }
        break;
    }

    return;
  }
}

auto main() -> int {
  window_context ctx{};
  ctx.width = 1920;
  ctx.height = 1080;

  ctx.video_fd = ::open("/dev/video0", O_RDWR);
  if (ctx.video_fd < 0) {
    perror_exit("open");
  }

  {
    ::v4l2_capability cap{};

    if (::ioctl(ctx.video_fd, VIDIOC_QUERYCAP, &cap) == -1) {
      perror_exit("VIDIOC_QUERYCAP");
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
      return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
      return -1;
    }
  }

  {
    ::v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.width = ctx.width;
    format.fmt.pix_mp.height = ctx.height;
    format.fmt.pix_mp.pixelformat = v4l2_fourcc('X', 'B', 'G', 'R');
    format.fmt.pix_mp.field = V4L2_FIELD_ANY;
    format.fmt.pix_mp.num_planes = 1;
    format.fmt.pix_mp.plane_fmt[0].bytesperline = 0;

    if (::ioctl(ctx.video_fd, VIDIOC_S_FMT, &format) == -1) {
      perror_exit("VIDIOC_S_FMT");
    }

    if (::ioctl(ctx.video_fd, VIDIOC_G_FMT, &format) == -1) {
      perror_exit("VIDIOC_G_FMT");
    }
  }

  {
    ::v4l2_requestbuffers req{};
    req.count = num_buffers;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (::ioctl(ctx.video_fd, VIDIOC_REQBUFS, &req) == -1) {
      if (errno == EINVAL) {
        return -1;
      } else {
        perror_exit("VIDIOC_REQBUFS");
      }
    }

    for (auto i = 0u; i < req.count; ++i) {
      ::v4l2_plane planes[VIDEO_MAX_PLANES];
      ::v4l2_buffer buf{};
      buf.index = i;
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.length = VIDEO_MAX_PLANES;
      buf.m.planes = planes;
      if (::ioctl(ctx.video_fd, VIDIOC_QUERYBUF, &buf) == -1) {
        perror_exit("VIDIOC_QUERYBUF");
      }

      void* mem = ::mmap(nullptr, buf.m.planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED,
                         ctx.video_fd, buf.m.planes[0].m.mem_offset);
      if (mem == MAP_FAILED) {
        perror_exit("mmap");
      }

      ::v4l2_exportbuffer exbuf{};
      exbuf.index = i;
      exbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      exbuf.plane = 0;
      if (::ioctl(ctx.video_fd, VIDIOC_EXPBUF, &exbuf) == -1) {
        perror_exit("VIDIOC_EXPBUF");
      }

      auto& bufinfo = ctx.video_buffers.at(i);
      bufinfo = {
          static_cast<std::uint8_t*>(mem), // mem
          buf.m.planes[0].length,          // length
          buf.m.planes[0].data_offset,     // offset
          exbuf.fd                         // fd
      };

      std::printf("buffer%d @ %p, length: %u, offset: %u, fd: %d\n", i, mem, bufinfo.length,
                  bufinfo.offset, bufinfo.fd);

      if (::ioctl(ctx.video_fd, VIDIOC_QBUF, &buf) == -1) {
        perror_exit("VIDIOC_QBUF");
      }
    }
  }

  ctx.display = ::wl_display_connect(nullptr);
  if (!ctx.display) {
    std::cerr << "failed to connect display" << std::endl;
    return -1;
  }

  ::wl_registry* registry = ::wl_display_get_registry(ctx.display);
  ::wl_registry_add_listener(registry, &registry_listener, &ctx);

  ::wl_display_dispatch(ctx.display);
  ::wl_display_roundtrip(ctx.display);
  if (!ctx.compositor || !ctx.subcompositor || !ctx.shell) {
    std::cerr << "failed to find compositor, subcompositor or shell" << std::endl;
    return -1;
  }

  std::tie(ctx.egl_display, ctx.egl_config, ctx.egl_context) = init_egl(ctx.display);

  ctx.main_surface =
      make_surface(ctx.compositor, std::tie(ctx.egl_display, ctx.egl_config, ctx.egl_context),
                   ctx.width, ctx.height);

  ctx.shell_surface = ::wl_shell_get_shell_surface(ctx.shell, ctx.main_surface.surface);
  ::wl_shell_surface_set_toplevel(ctx.shell_surface);
  ::wl_shell_surface_add_listener(ctx.shell_surface, &shell_surface_listener, &ctx);

  ctx.overlay_surface = make_subsurface(ctx.compositor, ctx.subcompositor,
                                        std::tie(ctx.egl_display, ctx.egl_config, ctx.egl_context),
                                        ctx.main_surface, ctx.width, ctx.height);

  if (!::eglMakeCurrent(ctx.egl_display, ctx.main_surface.egl_surface, ctx.main_surface.egl_surface,
                        ctx.egl_context)) {
    return -1;
  }

  if (!(::eglCreateImageKHR = get_egl_proc<::PFNEGLCREATEIMAGEKHRPROC>("eglCreateImageKHR"))) {
    std::cerr << "eglCreateImageKHR" << std::endl;
    return -1;
  }

  if (!(::eglDestroyImageKHR = get_egl_proc<::PFNEGLDESTROYIMAGEKHRPROC>("eglDestroyImageKHR"))) {
    std::cerr << "eglDestroyImageKHR" << std::endl;
    return -1;
  }

  if (!(::glEGLImageTargetTexture2DOES =
            get_egl_proc<::PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>("glEGLImageTargetTexture2DOES"))) {
    std::cerr << "glEGLImageTargetTexture2DOES" << std::endl;
    return -1;
  }

  ctx.texture.program = create_gl_program(vertex_shader_src, fragment_shader_src);
  if (!ctx.texture.program) {
    return -1;
  }

  ctx.texture.a_position = ::glGetAttribLocation(ctx.texture.program, "a_position");
  ctx.texture.a_tex_coord = ::glGetAttribLocation(ctx.texture.program, "a_texCoord");
  ctx.texture.s_texture = ::glGetUniformLocation(ctx.texture.program, "s_texture");

  {
    ::glGenTextures(num_buffers, ctx.texture.textures);
    for (auto i = 0u; i < num_buffers; ++i) {
      // clang-format off
      ::EGLint attrs[] = {
        EGL_IMAGE_PRESERVED_KHR,       EGL_TRUE,
        EGL_WIDTH,                     ctx.width,
        EGL_HEIGHT,                    ctx.height,
        EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_ABGR8888,
        EGL_DMA_BUF_PLANE0_FD_EXT,     ctx.video_buffers[i].fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<::EGLint>(ctx.video_buffers[i].offset),
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  ctx.width * 4,
        EGL_NONE
      };
      // clang-format on

      ::EGLImageKHR image = ::eglCreateImageKHR(ctx.egl_display, EGL_NO_CONTEXT,
                                                EGL_LINUX_DMA_BUF_EXT, nullptr, attrs);
      if (image == EGL_NO_IMAGE_KHR) {
        std::cerr << "failed to create image" << std::endl;
        return -1;
      }

      ::glBindTexture(GL_TEXTURE_EXTERNAL_OES, ctx.texture.textures[i]);
      ::glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
      ::glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    }
  }

  ::v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (::ioctl(ctx.video_fd, VIDIOC_STREAMON, &type) == -1) {
    perror_exit("VIDIOC_STREAMON");
  }

  ctx.cairo_device = ::cairo_egl_device_create(ctx.egl_display, ctx.egl_context);
  if (::cairo_device_status(ctx.cairo_device) != CAIRO_STATUS_SUCCESS) {
    std::cerr << "failed to create cairo egl device" << std::endl;
    return -1;
  }

  ctx.epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
  if (ctx.epoll_fd < 0) {
    perror_exit("epoll_create1");
  }

  ctx.display_fd = ::wl_display_get_fd(ctx.display);
  {
    ::epoll_event ep{};
    ep.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ep.data.ptr = reinterpret_cast<void*>(handle_display_events);
    ::epoll_ctl(ctx.epoll_fd, EPOLL_CTL_ADD, ctx.display_fd, &ep);
  }

  {
    ::epoll_event ep{};
    ep.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ep.data.ptr = reinterpret_cast<void*>(handle_v4l2_events);
    ::epoll_ctl(ctx.epoll_fd, EPOLL_CTL_ADD, ctx.video_fd, &ep);
  }

  ctx.timer_fd = ::timerfd_create(CLOCK_REALTIME, 0);
  if (ctx.timer_fd < 0) {
    perror_exit("timerfd_create");
  }

  {
    ::timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
      perror_exit("clock_gettime");
    }

    ::itimerspec nexttime{};
    nexttime.it_interval.tv_sec = 0;
    nexttime.it_interval.tv_nsec = 10'000'000; // 10 [ms]
    nexttime.it_value.tv_sec = nexttime.it_interval.tv_sec + now.tv_sec;
    nexttime.it_value.tv_nsec = nexttime.it_interval.tv_nsec + now.tv_nsec;

    if (::timerfd_settime(ctx.timer_fd, TFD_TIMER_ABSTIME, &nexttime, nullptr) != 0) {
      perror_exit("timerfd_settime");
    }
  }

  {
    ::epoll_event ep{};
    ep.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ep.data.ptr = reinterpret_cast<void*>(handle_timer_events);
    ::epoll_ctl(ctx.epoll_fd, EPOLL_CTL_ADD, ctx.timer_fd, &ep);
  }

  ctx.joystick_fd = ::open("/dev/input/js0", O_RDONLY);
  if (ctx.joystick_fd < 0) {
    std::cerr << "failed to open /dev/input/js0: " << std::strerror(errno) << std::endl;
  } else {
    if (::ioctl(ctx.joystick_fd, JSIOCGAXES, &ctx.joystick.num_axes) < 0) {
      perror_exit("JSIOCGAXES");
    }
    if (::ioctl(ctx.joystick_fd, JSIOCGBUTTONS, &ctx.joystick.num_buttons) < 0) {
      perror_exit("JSIOCGBUTTONS");
    }

    ctx.joystick.axes = std::make_unique<std::int16_t[]>(ctx.joystick.num_axes);
    ctx.joystick.buttons = std::make_unique<std::int16_t[]>(ctx.joystick.num_buttons);

    ::epoll_event ep{};
    ep.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ep.data.ptr = reinterpret_cast<void*>(handle_joystick_events);
    ::epoll_ctl(ctx.epoll_fd, EPOLL_CTL_ADD, ctx.joystick_fd, &ep);
  }

  ctx.app.cr = -0.4;
  ctx.app.ci = 0.6;
  ctx.app.scale = 1.0;
  ctx.app.offset_x = 0.0;
  ctx.app.offset_y = 0.0;

  ctx.fractal_ctl = std::make_unique<fractal_controller>(0xa0000000);

  redraw(&ctx, nullptr, 0);

  ctx.running = true;
  for (;;) {
    ::wl_display_dispatch_pending(ctx.display);
    if (!ctx.running) {
      break;
    }

    int ret = ::wl_display_flush(ctx.display);
    if (ret < 0 && errno == EAGAIN) {
      ::epoll_event ep{};
      ep.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP;
      ep.data.ptr = &ctx;
      ::epoll_ctl(ctx.epoll_fd, EPOLL_CTL_MOD, ctx.display_fd, &ep);
    } else if (ret < 0) {
      break;
    }

    ::epoll_event ep[16];
    int count = ::epoll_wait(ctx.epoll_fd, ep, 16, -1);
    for (int i = 0; i < count; ++i) {
      using handler_type = void (*)(window_context*, std::uint32_t);
      reinterpret_cast<handler_type>(ep[i].data.ptr)(&ctx, ep[i].events);
    }
  }

  ::wl_display_disconnect(ctx.display);
}
