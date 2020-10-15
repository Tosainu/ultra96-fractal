#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <drm_fourcc.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

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

using namespace std::chrono_literals;
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

  int drm_fd;
  std::uint32_t crtc_id;
  std::uint32_t connector_id;
  ::drmModeModeInfo display_mode;

  ::gbm_device* gbm_device;
  ::gbm_surface* gbm_surface;
  ::gbm_bo* gbm_bo;
  ::gbm_bo* gbm_bo_next;
  std::uint32_t fb_id;
  std::uint32_t fb_id_next;

  ::EGLDisplay egl_display;
  ::EGLConfig egl_config;
  ::EGLContext egl_context;
  ::EGLSurface egl_surface;

  struct {
    ::GLuint program;
    ::GLuint a_position;
    ::GLuint a_tex_coord;
    ::GLuint s_texture;
    ::GLuint textures[num_buffers];
  } texture;

  ::cairo_device_t* cairo_device;
  ::cairo_surface_t* cairo_surface;

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

  float v4l2_fps;
  std::uint64_t v4l2_total_frames;
  std::chrono::steady_clock::time_point v4l2_fps_updated_time;

  float display_fps;
  std::uint64_t display_total_frames;
  std::uint64_t display_fps_updated_time;

  bool running;
  int epoll_fd;
  int display_fd;
  int timer_fd;
  int joystick_fd;

  struct app_state {
    bool animation;
    std::uint64_t animation_frame;
    double cr, ci, scale, scale_q, offset_x, offset_y;
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

  explicit fix(double v) : value_{double_to_fix(v)} {}

  static std::uint32_t double_to_fix(double v) {
    struct {
      std::uint64_t frac : 52;
      std::uint64_t exp : 11;
      std::uint64_t sign : 1;
    } s;
    std::memcpy(&s, &v, sizeof v);

    const auto frac = static_cast<std::uint64_t>(s.frac) | (1ul << 52);
    const auto exp = static_cast<std::int16_t>(s.exp) - 1023;

    std::size_t shift;
    if (std::int32_t s = 52 - exp; static_cast<std::int32_t>(fractional_width) > s) {
      shift = fractional_width - s;
    } else {
      shift = s - fractional_width;
    }

    std::uint32_t ret;
    if (shift >= 0) {
      ret = frac >> shift;
    } else {
      ret = frac << -shift;
    }

    return s.sign ? -ret : ret;
  }

  inline std::uint32_t value() const {
    return value_;
  }

  inline double to_double() const {
    if (!value_) {
      return 0.0;
    }
    return static_cast<double>(static_cast<std::int32_t>(value_)) / (1ul << fractional_width);
  }

  inline std::int32_t integer() const {
    return static_cast<std::int32_t>(value_) / (1ul << fractional_width);
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

enum class color_mode : std::uint8_t {
  gray,
  red,
  green,
  blue,
  yellow,
  cyan,
  magenta,
  color1,
};

color_mode next_mode(color_mode m) {
  if (m == color_mode::color1) {
    return color_mode::gray;
  } else {
    return static_cast<color_mode>(static_cast<std::uint8_t>(m) + 1);
  }
}

color_mode prev_mode(color_mode m) {
  if (m == color_mode::gray) {
    return color_mode::color1;
  } else {
    return static_cast<color_mode>(static_cast<std::uint8_t>(m) - 1);
  }
}

class fractal_controller {
  int fd_;
  std::size_t size_;
  std::uint32_t* reg_;

public:
  fractal_controller(std::uintptr_t base_addr)
      : fd_{-1}, reg_{static_cast<std::uint32_t*>(MAP_FAILED)} {
    fd_ = ::open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_ < 0) {
      throw std::runtime_error{"failed to open /dev/mem: "s + std::strerror(errno)};
    }

    const auto s = ::sysconf(_SC_PAGESIZE);
    if (s < 0) {
      throw std::runtime_error{"failed to get page size: "s + std::strerror(errno)};
    }
    size_ = static_cast<std::size_t>(s);

    reg_ = static_cast<std::uint32_t*>(
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

  color_mode mode() const {
    return static_cast<color_mode>((reg_[0] & 0xf00) >> 8);
  }

  void set_mode(color_mode mode) {
    reg_[0] = (static_cast<std::uint32_t>(mode) << 8) | (reg_[0] & ~0xf00);
  }

#define FRACTAL_CONTROLLER_GETTER_SETTER(name, index) \
  fix<4> name() const {                               \
    return fix<4>{reg_[index]};                       \
  }                                                   \
  void set_##name(double name) {                      \
    reg_[index] = fix<4>::double_to_fix(name);        \
  }

  FRACTAL_CONTROLLER_GETTER_SETTER(x0, 4u)
  FRACTAL_CONTROLLER_GETTER_SETTER(y0, 6u)
  FRACTAL_CONTROLLER_GETTER_SETTER(dx, 8u)
  FRACTAL_CONTROLLER_GETTER_SETTER(dy, 10u)
  FRACTAL_CONTROLLER_GETTER_SETTER(cr, 12u)
  FRACTAL_CONTROLLER_GETTER_SETTER(ci, 14u)

#undef FRACTAL_CONTROLLER_GETTER_SETTER
};

std::tuple<std::uint32_t, std::uint32_t, ::drmModeModeInfo> init_drm(int fd) {
  auto resources = ::drmModeGetResources(fd);
  if (!resources) {
    throw std::runtime_error{"drmModeGetResources: "s + std::strerror(errno)};
  }

  ::drmModeConnector* connector{};
  for (int i = 0; i < resources->count_connectors; ++i) {
    connector = ::drmModeGetConnector(fd, resources->connectors[i]);
    if (!connector) continue;
    if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) break;
    ::drmModeFreeConnector(connector);
    connector = nullptr;
  }
  if (!connector) {
    throw std::runtime_error{"connected connector not found"};
  }

  std::uint32_t crtc_id{};
  if (connector->encoder_id) {
    auto e = ::drmModeGetEncoder(fd, connector->encoder_id);
    crtc_id = e->crtc_id;
    ::drmModeFreeEncoder(e);
  } else {
    bool crtc_found = false;

    for (int i = 0; i < resources->count_encoders; ++i) {
      auto e = ::drmModeGetEncoder(fd, resources->encoders[i]);
      if (!e) continue;
      for (int j = 0; j < resources->count_crtcs; ++j) {
        if (e->possible_crtcs & (1 << j)) {
          crtc_found = true;
          crtc_id = resources->crtcs[j];
          break;
        }
      }
      ::drmModeFreeEncoder(e);
      if (crtc_found) break;
    }

    if (!crtc_found) {
      throw std::runtime_error{"crtc not found"};
    }
  }

  auto mode = connector->modes[0];
  std::uint32_t connector_id = connector->connector_id;

  ::drmModeFreeConnector(connector);

  return std::make_tuple(crtc_id, connector_id, mode);
}

std::tuple<::EGLDisplay, ::EGLConfig, ::EGLContext> init_egl(::EGLDisplay display) {
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

std::tuple<::EGLDisplay, ::EGLConfig, ::EGLContext> init_egl(::gbm_device* gbm) {
  auto get_platform_display_ext =
      get_egl_proc<::PFNEGLGETPLATFORMDISPLAYEXTPROC>("eglGetPlatformDisplayEXT");
  if (!get_platform_display_ext) {
    throw std::runtime_error{"failed to get eglGetPlatformDisplayEXT"};
  }

  ::EGLDisplay display = get_platform_display_ext(EGL_PLATFORM_GBM_KHR, gbm, nullptr);
  if (!display) {
    throw std::runtime_error{"failed to create egl display"};
  }

  return init_egl(display);
}

static std::uint32_t get_gbm_bo_fb_id(int drm_fd, ::gbm_bo* bo) {
  const auto width = ::gbm_bo_get_width(bo);
  const auto height = ::gbm_bo_get_height(bo);
  const std::uint32_t handles[4] = {::gbm_bo_get_handle(bo).u32};
  const std::uint32_t strides[4] = {::gbm_bo_get_stride(bo)};
  const std::uint32_t offsets[4] = {};

  std::uint32_t fb_id{};
  if (::drmModeAddFB2(drm_fd, width, height, DRM_FORMAT_ARGB8888, handles, strides, offsets, &fb_id,
                      0)) {
    throw std::runtime_error{"drmModeAddFB2: "s + std::strerror(errno)};
  }

  return fb_id;
}

static void redraw_main_surface(::window_context* ctx) {
  if (!::eglMakeCurrent(ctx->egl_display, ctx->egl_surface, ctx->egl_surface, ctx->egl_context)) {
    std::cerr << "eglMakeCurrent failed" << std::endl;
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

static void flush_main_surface(::window_context* ctx) {
  if (!::eglMakeCurrent(ctx->egl_display, ctx->egl_surface, ctx->egl_surface, ctx->egl_context)) {
    std::cerr << "eglMakeCurrent failed" << std::endl;
    return;
  }

  ::cairo_gl_surface_swapbuffers(ctx->cairo_surface);
  ::glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
}

static void redraw_overlay_surface(::window_context* ctx) {
  if (!::eglMakeCurrent(ctx->egl_display, ctx->egl_surface, ctx->egl_surface, ctx->egl_context)) {
    std::cerr << "eglMakeCurrent failed" << std::endl;
    return;
  }

  const auto width = ctx->width;
  const auto height = ctx->height;

  if (!ctx->cairo_surface) {
    ctx->cairo_surface =
        ::cairo_gl_surface_create_for_egl(ctx->cairo_device, ctx->egl_surface, width, height);
  }
  auto cr = ::cairo_create(ctx->cairo_surface);

  ::cairo_set_source_rgba(cr, 0.125, 0.125, 0.125, 0.75);
  ::cairo_rectangle(cr, 31.5, 63.5, 497, 149);
  ::cairo_fill_preserve(cr);

  ::cairo_set_line_width(cr, 1.0);
  ::cairo_set_source_rgba(cr, 0, 0, 0, 1.0);
  ::cairo_stroke(cr);

  ::cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  ::cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

  ::cairo_set_font_size(cr, 32);
  ::cairo_move_to(cr, 48.0, 104.0);
  ::cairo_show_text(cr, "Julia Set Explorer");

  ::cairo_set_font_size(cr, 16);
  ::cairo_move_to(cr, 400.0, 104.0);
  ::cairo_show_text(cr, "by @myon___");

  constexpr auto max_len = 255;
  char str[max_len] = {};
  std::snprintf(str, max_len,
                "c: %12.8f%+.8fi\n"
                "x: %12.8f,  y:  %12.8f,  scale: %12.8f\n"
                "\n"
                "fps (fpga / display): %.4f / %.4f",
                ctx->app.cr, ctx->app.ci, ctx->app.offset_x, ctx->app.offset_y, ctx->app.scale,
                ctx->v4l2_fps, ctx->display_fps);

  ::cairo_set_font_size(cr, 13);
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

static void redraw(void* data) {
  auto ctx = static_cast<::window_context*>(data);

  redraw_main_surface(ctx);
  redraw_overlay_surface(ctx);

  flush_main_surface(ctx);
}

static void drm_page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec,
                                  void* data) {
  auto ctx = static_cast<window_context*>(data);

  if (!ctx->gbm_bo_next) {
    ::drmModeRmFB(ctx->drm_fd, ctx->fb_id);
    ctx->fb_id = ctx->fb_id_next;

    ::gbm_surface_release_buffer(ctx->gbm_surface, ctx->gbm_bo);
    ctx->gbm_bo = ctx->gbm_bo_next;
    ctx->gbm_bo_next = nullptr;
  }

  if (++ctx->display_total_frames % 5 == 0) {
    const auto time = static_cast<std::uint64_t>(sec) * 1'000'000 + usec;
    ctx->display_fps = 5'000'000.0f / (time - ctx->display_fps_updated_time);
    ctx->display_fps_updated_time = time;
  }

  redraw(ctx);

  ctx->gbm_bo_next = ::gbm_surface_lock_front_buffer(ctx->gbm_surface);
  ctx->fb_id_next = get_gbm_bo_fb_id(ctx->drm_fd, ctx->gbm_bo_next);

  if (::drmModePageFlip(ctx->drm_fd, ctx->crtc_id, ctx->fb_id_next, DRM_MODE_PAGE_FLIP_EVENT,
                        ctx)) {
    std::cerr << "failed to queue page flip: " << std::strerror(errno) << std::endl;
    ctx->running = false;
  }
}

static ::drmEventContext drm_event_context;

static void handle_drm_events(window_context* ctx, std::uint32_t events) {
  if (events & EPOLLERR || events & EPOLLHUP) {
    ctx->running = false;
    return;
  }

  if (!(events & EPOLLIN)) {
    return;
  }

  drm_event_context.version = DRM_EVENT_CONTEXT_VERSION;
  drm_event_context.page_flip_handler = drm_page_flip_handler;

  ::drmHandleEvent(ctx->drm_fd, &drm_event_context);
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

  if (++ctx->v4l2_total_frames % 5 == 0) {
    const auto now = std::chrono::steady_clock::now();
    ctx->v4l2_fps = 5'000'000.0f / ((now - ctx->v4l2_fps_updated_time) / 1us);
    ctx->v4l2_fps_updated_time = now;
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

    auto& app = ctx->app;

    double shift_x = 0;
    double shift_y = 0;

    if (ctx->joystick_fd >= 0) {
      const auto& j = ctx->joystick;

      if (j.buttons[1] && app.scale_q >= -2.0) app.scale_q -= 0.001;
      if (j.buttons[2] && app.scale_q <= 7.25) app.scale_q += 0.001;

      if (j.axes[4] > 0) shift_x += 2.0;
      if (j.axes[4] < 0) shift_x -= 2.0;

      if (j.axes[5] < 0) shift_y += 2.0;
      if (j.axes[5] > 0) shift_y -= 2.0;
    }

    app.scale = std::exp(app.scale_q - 1.0);

    {
      constexpr auto ratio = 1080.0 / 1920.0;
      const auto scale_inv = 1.0 / app.scale;
      const auto x1 = 1.0 * scale_inv;
      const auto y1 = ratio * scale_inv;
      const auto dx = 2.0 * x1 / 1920.0;
      const auto dy = 2.0 * y1 / 1080.0;

      app.offset_x += dx * shift_x;
      app.offset_y += dy * shift_y;
      const auto x0 = x1 - app.offset_x;
      const auto y0 = y1 + app.offset_y;

      ctx->fractal_ctl->set_x0(x0);
      ctx->fractal_ctl->set_y0(y0);
      ctx->fractal_ctl->set_dx(dx);
      ctx->fractal_ctl->set_dy(dy);
    }

    if (app.animation) {
      const auto i = (app.animation_frame + exp) % 10000;

      const auto t = (static_cast<double>(i) / 10000) * 6.28;
      app.cr = 0.7885 * std::cos(t);
      app.ci = 0.7885 * std::sin(t);

      app.animation_frame = i;
    } else {
      app.cr = -0.4;
      app.ci = 0.6;
    }
    ctx->fractal_ctl->set_cr(app.cr);
    ctx->fractal_ctl->set_ci(app.ci);
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

        if (jse.number == 4 && jse.value) {
          ctx->fractal_ctl->set_mode(prev_mode(ctx->fractal_ctl->mode()));
        }
        if (jse.number == 5 && jse.value) {
          ctx->fractal_ctl->set_mode(next_mode(ctx->fractal_ctl->mode()));
        }

        if (jse.number == 8 && jse.value) {
          ctx->app.scale = 1.0;
          ctx->app.scale_q = 1.0;
          ctx->app.offset_x = 0.0;
          ctx->app.offset_y = 0.0;
        }

        if (jse.number == 9 && jse.value) {
          ctx->app.animation = !ctx->app.animation;
          ctx->app.animation_frame = 0;
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
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_BGRX32;
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

  ctx.drm_fd = ::open("/dev/dri/card0", O_RDWR);
  if (ctx.drm_fd < 0) {
    perror_exit("open");
  }

  std::tie(ctx.crtc_id, ctx.connector_id, ctx.display_mode) = init_drm(ctx.drm_fd);
  std::cout << "connector: " << ctx.connector_id << ", mode: " << ctx.display_mode.hdisplay << 'x'
            << ctx.display_mode.vdisplay << ", crtc: " << ctx.crtc_id << std::endl;

  ctx.gbm_device = ::gbm_create_device(ctx.drm_fd);
  if (!ctx.gbm_device) {
    return -1;
  }

  ctx.gbm_surface =
      ::gbm_surface_create(ctx.gbm_device, ctx.display_mode.hdisplay, ctx.display_mode.vdisplay,
                           GBM_FORMAT_ARGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  if (!ctx.gbm_surface) {
    std::cerr << "gbm_surface_create: failed to create gbm surface" << std::endl;
    return -1;
  }

  std::tie(ctx.egl_display, ctx.egl_config, ctx.egl_context) = init_egl(ctx.gbm_device);

  ctx.egl_surface =
      ::eglCreateWindowSurface(ctx.egl_display, ctx.egl_config,
                               reinterpret_cast<::EGLNativeWindowType>(ctx.gbm_surface), nullptr);
  if (ctx.egl_surface == EGL_NO_SURFACE) {
    std::cerr << "failed to create egl surface" << std::endl;
    return -1;
  }

  if (!::eglMakeCurrent(ctx.egl_display, ctx.egl_surface, ctx.egl_surface, ctx.egl_context)) {
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

  ctx.v4l2_fps = 0.0f;
  ctx.v4l2_total_frames = 0;
  ctx.v4l2_fps_updated_time = std::chrono::steady_clock::now();

  ::v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (::ioctl(ctx.video_fd, VIDIOC_STREAMON, &type) == -1) {
    perror_exit("VIDIOC_STREAMON");
  }

  ctx.cairo_device = ::cairo_egl_device_create(ctx.egl_display, ctx.egl_context);
  if (::cairo_device_status(ctx.cairo_device) != CAIRO_STATUS_SUCCESS) {
    std::cerr << "failed to create cairo egl device" << std::endl;
    return -1;
  }
  ctx.cairo_surface = nullptr;

  ctx.epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
  if (ctx.epoll_fd < 0) {
    perror_exit("epoll_create1");
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

  ctx.app.animation = false;
  ctx.app.scale = 1.0;
  ctx.app.scale_q = 1.0;
  ctx.app.offset_x = 0.0;
  ctx.app.offset_y = 0.0;

  ctx.fractal_ctl = std::make_unique<fractal_controller>(0xa0000000);

  ctx.display_fps = 0.0f;
  ctx.display_total_frames = 0;
  ctx.display_fps_updated_time = 0;

  {
    ::glClearColor(0.0, 0.0, 0.0, 1.0);
    ::glClear(GL_COLOR_BUFFER_BIT);
    ::eglSwapBuffers(ctx.egl_display, ctx.egl_surface);

    ctx.gbm_bo = ::gbm_surface_lock_front_buffer(ctx.gbm_surface);
    ctx.fb_id = get_gbm_bo_fb_id(ctx.drm_fd, ctx.gbm_bo);

    if (::drmModeSetCrtc(ctx.drm_fd, ctx.crtc_id, ctx.fb_id, 0, 0, &ctx.connector_id, 1,
                         &ctx.display_mode)) {
      std::cerr << "drmModeSetCrtc: " << std::strerror(errno) << std::endl;
      return -1;
    }
  }

  {
    ::epoll_event ep{};
    ep.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ep.data.ptr = reinterpret_cast<void*>(handle_drm_events);
    ::epoll_ctl(ctx.epoll_fd, EPOLL_CTL_ADD, ctx.drm_fd, &ep);
  }

  {
    redraw(&ctx);

    ctx.gbm_bo_next = ::gbm_surface_lock_front_buffer(ctx.gbm_surface);
    ctx.fb_id_next = get_gbm_bo_fb_id(ctx.drm_fd, ctx.gbm_bo_next);

    if (::drmModePageFlip(ctx.drm_fd, ctx.crtc_id, ctx.fb_id_next, DRM_MODE_PAGE_FLIP_EVENT,
                          &ctx)) {
      std::cerr << "failed to queue page flip: " << std::strerror(errno) << std::endl;
      return -1;
    }
  }

  ctx.running = true;
  for (;;) {
    if (!ctx.running) {
      break;
    }

    ::epoll_event ep[16];
    int count = ::epoll_wait(ctx.epoll_fd, ep, 16, -1);
    for (int i = 0; i < count; ++i) {
      using handler_type = void (*)(window_context*, std::uint32_t);
      reinterpret_cast<handler_type>(ep[i].data.ptr)(&ctx, ep[i].events);
    }
  }
}
