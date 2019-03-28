#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define NANOVG_GLES2_IMPLEMENTATION
#include <nanovg.h>
#include <nanovg_gl.h>

extern "C" {
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <libdrm/drm_fourcc.h>

#include <linux/videodev2.h>
}

using namespace std::string_view_literals;

static constexpr std::uint32_t NUM_BUFFERS = 8;

static ::PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
static ::PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
static ::PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

template <class T>
static inline T get_egl_proc(const std::string_view proc) {
  return reinterpret_cast<T>(::eglGetProcAddress(proc.data()));
}

struct window_context {
  int width;
  int height;

  ::wl_display* display;

  ::wl_compositor* compositor;
  ::wl_egl_window* egl_window;
  ::wl_region* region;
  ::wl_shell* shell;
  ::wl_shell_surface* shell_surface;
  ::wl_surface* surface;
  ::wl_callback* redraw_cb;

  ::EGLDisplay egl_display;
  ::EGLContext egl_context;
  ::EGLSurface egl_surface;

  ::NVGcontext* vg;

  struct texture_context {
    ::GLuint program;
    ::GLuint a_position;
    ::GLuint a_tex_coord;
    ::GLuint s_texture;
    ::GLuint textures[NUM_BUFFERS];
  } texture;

  int video_fd;
  struct buffer_context {
    std::uint8_t* ptr;
    std::uint32_t length;
    std::uint32_t offset;
    int fd;
  };
  std::array<buffer_context, NUM_BUFFERS> video_buffers;
};

static ::GLuint load_shader(::GLenum type, const char* str) {
  ::GLuint shader = ::glCreateShader(type);
  if (!shader) {
    return 0;
  }

  ::glShaderSource(shader, 1, &str, nullptr);
  ::glCompileShader(shader);

  ::GLint compiled;
  ::glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    ::glDeleteShader(shader);
    return 0;
  }

  return shader;
}

std::uint32_t double_to_fix32_4(double v) {
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
  int ap_f = 32 - 4;
  int f2 = ap_w2 - ap_i2;

  unsigned shift = f2 > ap_f ? f2 - ap_f : ap_f - f2;

  if (f2 == ap_f) {
    return frac;
  } else if (f2 > ap_f) {
    if (shift < ap_w2) {
      return frac >> shift;
    } else {
      return df.s.sign ? -1 : 0;
    }
  } else {
    if (shift < 32) {
      return frac << shift;
    } else {
      return 0;
    }
  }
}

void handle_ping(void*, ::wl_shell_surface* shell_surface, std::uint32_t serial) {
  wl_shell_surface_pong(shell_surface, serial);
}

void handle_configure(void*, ::wl_shell_surface*, std::uint32_t, std::int32_t, std::int32_t) {}

void handle_popup_done(void*, ::wl_shell_surface*) {}

const struct wl_shell_surface_listener shell_surface_listener = {handle_ping, handle_configure,
                                                                 handle_popup_done};

static void registry_handle_global(void* data, ::wl_registry* registry, std::uint32_t id,
                                   const char* interface,
                                   [[maybe_unused]] std::uint32_t version) {
  const auto ifname = std::string_view{interface};
  auto ctx = static_cast<::window_context*>(data);

  if (ifname == "wl_compositor"sv) {
    ctx->compositor = static_cast<::wl_compositor*>(
        ::wl_registry_bind(registry, id, &wl_compositor_interface, 1));
  } else if (ifname == "wl_shell"sv) {
    ctx->shell =
        static_cast<::wl_shell*>(::wl_registry_bind(registry, id, &wl_shell_interface, 1));
  }
}

static void registry_handle_global_remove(void*, ::wl_registry*, uint32_t) {}

static const ::wl_registry_listener registry_listener = {registry_handle_global,
                                                         registry_handle_global_remove};

static inline void perror_exit(std::string_view s) {
  std::perror(s.data());
  std::exit(EXIT_FAILURE);
}

static void redraw(void* data, ::wl_callback* callback, std::uint32_t time);

static const ::wl_callback_listener listener = {
    redraw,
};

static void redraw(void* data, ::wl_callback* callback, std::uint32_t time) {
  ::window_context* ctx = reinterpret_cast<::window_context*>(data);

  if (callback) {
    ::wl_callback_destroy(callback);
  }

  const auto width = ctx->width;
  const auto height = ctx->height;

  ::v4l2_plane planes[VIDEO_MAX_PLANES];
  ::v4l2_buffer buf{};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.length = VIDEO_MAX_PLANES;
  buf.m.planes = planes;
  if (::ioctl(ctx->video_fd, VIDIOC_DQBUF, &buf) == -1) {
    perror_exit("VIDIOC_DQBUF");
  }

  const auto t1 = std::chrono::steady_clock::now();

  ::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  ::glClear(GL_COLOR_BUFFER_BIT);

  {
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

    ::glVertexAttribPointer(t.a_position, 3, GL_FLOAT, GL_FALSE, 0, tex_pos);
    ::glVertexAttribPointer(t.a_tex_coord, 2, GL_FLOAT, GL_FALSE, 0, tex_coord);

    ::glEnableVertexAttribArray(t.a_position);
    ::glEnableVertexAttribArray(t.a_tex_coord);

    ::glBindTexture(GL_TEXTURE_EXTERNAL_OES, t.textures[buf.index]);
    ::glUniform1i(t.s_texture, 0);
    ::glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    ::glDisableVertexAttribArray(t.a_position);
    ::glDisableVertexAttribArray(t.a_tex_coord);
    ::glUseProgram(0);
  }

  ::nvgBeginFrame(ctx->vg, width, height, 1.0f);
  {
    ::nvgBeginPath(ctx->vg);
    ::nvgRect(ctx->vg, 32, 64, 436, 152);
    ::nvgFillColor(ctx->vg, nvgRGBA(32, 32, 32, 192));
    ::nvgFill(ctx->vg);

    ::nvgBeginPath(ctx->vg);
    ::nvgRect(ctx->vg, 31.5f, 63.5f, 437, 153);
    ::nvgStrokeWidth(ctx->vg, 1.0f);
    ::nvgStrokeColor(ctx->vg, nvgRGBA(0, 0, 0, 255));
    ::nvgStroke(ctx->vg);

    nvgFontFace(ctx->vg, "mono");
    nvgFillColor(ctx->vg, nvgRGBA(255, 255, 255, 255));

    nvgFontSize(ctx->vg, 32);
    nvgText(ctx->vg, 48, 104, "Julia Set Explorer", nullptr);

    nvgFontSize(ctx->vg, 16);
    nvgText(ctx->vg, 364, 104, "by @myon___", nullptr);

    nvgFontSize(ctx->vg, 14);
    nvgText(ctx->vg, 48, 134, "cr: +00.000000000000    ci: +00.000000000000", nullptr);
    nvgText(ctx->vg, 48, 154, "x0: +00.000000000000    dx: +00.000000000000", nullptr);
    nvgText(ctx->vg, 48, 174, "y0: +00.000000000000    dy: +00.000000000000", nullptr);
    nvgText(ctx->vg, 48, 194, "fps: 000.000, 000.000", nullptr);
  }
  ::nvgEndFrame(ctx->vg);

  ::eglSwapBuffers(ctx->egl_display, ctx->egl_surface);
  ::glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

  if (::ioctl(ctx->video_fd, VIDIOC_QBUF, &buf) == -1) {
    perror_exit("VIDIOC_QBUF");
  }

  const auto t2 = std::chrono::steady_clock::now();
  const auto t1t2 = (t2 - t1) / std::chrono::nanoseconds(1);
  std::cout << std::setw(12) << t1t2 << std::endl;

  ctx->redraw_cb = ::wl_surface_frame(ctx->surface);
  ::wl_callback_add_listener(ctx->redraw_cb, &listener, ctx);
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
    req.count = NUM_BUFFERS;
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

      std::printf("buffer%d @ %p, length: %u, offset: %u, fd: %d\n", i, bufinfo.ptr, bufinfo.length,
                  bufinfo.offset, bufinfo.fd);

      if (::ioctl(ctx.video_fd, VIDIOC_QBUF, &buf) == -1) {
        perror_exit("VIDIOC_QBUF");
      }
    }
  }

  // ------- Wayland -------

  ctx.display = ::wl_display_connect(nullptr);
  if (!ctx.display) {
    std::cerr << "failed to connect display" << std::endl;
    return -1;
  }

  ::wl_registry* registry = ::wl_display_get_registry(ctx.display);

  ::wl_registry_add_listener(registry, &registry_listener, &ctx);

  ::wl_display_dispatch(ctx.display);
  ::wl_display_roundtrip(ctx.display);
  if (!ctx.compositor || !ctx.shell) {
    std::cerr << "failed to find compositor or shell" << std::endl;
    return -1;
  }

  ctx.surface = ::wl_compositor_create_surface(ctx.compositor);
  if (!ctx.surface) {
    std::cerr << "failed to create surface" << std::endl;
    return -1;
  }

  ctx.shell_surface = ::wl_shell_get_shell_surface(ctx.shell, ctx.surface);
  ::wl_shell_surface_set_toplevel(ctx.shell_surface);
  ::wl_shell_surface_add_listener(ctx.shell_surface, &shell_surface_listener, &ctx);

  // clang-format off
  static const ::EGLint config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RED_SIZE,        1,
    EGL_GREEN_SIZE,      1,
    EGL_BLUE_SIZE,       1,
    EGL_ALPHA_SIZE,      1,
    EGL_DEPTH_SIZE,      1,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };

  static const ::EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE};
  // clang-format on

  ctx.egl_display = ::eglGetDisplay(static_cast<::EGLNativeDisplayType>(ctx.display));
  if (!ctx.egl_display) {
    std::cerr << "failed to create egl display" << std::endl;
    return -1;
  }

  if (::eglInitialize(ctx.egl_display, nullptr, nullptr) != EGL_TRUE) {
    std::cerr << "failed to initialize egl display" << std::endl;
    return -1;
  }

  if (!::eglBindAPI(EGL_OPENGL_ES_API)) {
    std::cerr << "failed to bind EGL client API" << std::endl;
    return -1;
  }

  ::EGLConfig configs;
  ::EGLint num_configs{};
  ::eglChooseConfig(ctx.egl_display, config_attribs, &configs, 1, &num_configs);
  if (!num_configs) {
    return -1;
  }

  ctx.egl_window = wl_egl_window_create(ctx.surface, ctx.width, ctx.height);
  if (!ctx.egl_window) {
    std::cerr << "failed to create egl window" << std::endl;
    return -1;
  }

  ctx.egl_context =
      ::eglCreateContext(ctx.egl_display, configs, EGL_NO_CONTEXT, context_attribs);

  ctx.egl_surface = ::eglCreateWindowSurface(
      ctx.egl_display, configs, static_cast<::EGLNativeWindowType>(ctx.egl_window), nullptr);
  if (ctx.egl_surface == EGL_NO_SURFACE) {
    return -1;
  }

  if (!::eglMakeCurrent(ctx.egl_display, ctx.egl_surface, ctx.egl_surface, ctx.egl_context)) {
    return -1;
  }

  if (!(::eglCreateImageKHR = get_egl_proc<::PFNEGLCREATEIMAGEKHRPROC>("eglCreateImageKHR"sv))) {
    std::cerr << "eglCreateImageKHR" << std::endl;
    return -1;
  }

  if (!(::eglDestroyImageKHR = get_egl_proc<::PFNEGLDESTROYIMAGEKHRPROC>("eglDestroyImageKHR"sv))) {
    std::cerr << "eglDestroyImageKHR" << std::endl;
    return -1;
  }

  if (!(::glEGLImageTargetTexture2DOES = get_egl_proc<::PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            "glEGLImageTargetTexture2DOES"sv))) {
    std::cerr << "glEGLImageTargetTexture2DOES" << std::endl;
    return -1;
  }

  {
    auto vshader = load_shader(GL_VERTEX_SHADER, R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main()
{
   gl_Position = a_position;
   v_texCoord = a_texCoord;
}
)");
    if (!vshader) {
      std::cerr << "failed to load vertex shader" << std::endl;
      return -1;
    }

    auto fshader = load_shader(GL_FRAGMENT_SHADER, R"(
#extension GL_OES_EGL_image_external: require
precision mediump float;
varying vec2 v_texCoord;
uniform samplerExternalOES s_texture;
void main()
{
  gl_FragColor = texture2D(s_texture, v_texCoord);
}
)");
    if (!fshader) {
      std::cerr << "failed to load fragment shader" << std::endl;
      ::glDeleteShader(vshader);
      return -1;
    }

    ctx.texture.program = ::glCreateProgram();
    if (!ctx.texture.program) {
      std::cerr << "failed to create glprogram" << std::endl;
      ::glDeleteShader(vshader);
      ::glDeleteShader(fshader);
      return -1;
    }

    ::glAttachShader(ctx.texture.program, vshader);
    ::glAttachShader(ctx.texture.program, fshader);
    ::glLinkProgram(ctx.texture.program);

    ::GLint linked{};
    ::glGetProgramiv(ctx.texture.program, GL_LINK_STATUS, &linked);

    ::glDeleteShader(vshader);
    ::glDeleteShader(fshader);

    if (!linked) {
      std::cerr << "failed to link glprogram" << std::endl;
      ::glDeleteProgram(ctx.texture.program);
      return -1;
    }

    ctx.texture.a_position = ::glGetAttribLocation(ctx.texture.program, "a_position");
    ctx.texture.a_tex_coord = ::glGetAttribLocation(ctx.texture.program, "a_texCoord");
    ctx.texture.s_texture = ::glGetUniformLocation(ctx.texture.program, "s_texture");

    ::glGenTextures(NUM_BUFFERS, ctx.texture.textures);
    for (auto i = 0u; i < NUM_BUFFERS; ++i) {
      // clang-format off
      ::EGLint attrs[] = {
        EGL_WIDTH,                     ctx.width,
        EGL_HEIGHT,                    ctx.height,
        EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_ABGR8888,
        EGL_DMA_BUF_PLANE0_FD_EXT,     ctx.video_buffers[i].fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, ctx.video_buffers[i].offset,
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

  ctx.vg = ::nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
  if (!ctx.vg) {
    std::cerr << "failed to initialize nanovg" << std::endl;
    return -1;
  }

  ::nvgCreateFont(ctx.vg, "mono", "/usr/share/fonts/ttf/LiberationMono-Regular.ttf");

  int fractal_reg_fd = ::open("/dev/mem", O_RDWR | O_SYNC);
  if (fractal_reg_fd < 0) {
    perror_exit("open");
  }

  auto fractal_reg =
      reinterpret_cast<std::uint64_t*>(::mmap(nullptr, ::getpagesize(), PROT_READ | PROT_WRITE,
                                              MAP_SHARED, fractal_reg_fd, 0xa0000000));
  if (fractal_reg == MAP_FAILED) {
    perror_exit("mmap");
  }

  std::thread th([fractal_reg] {
    for (;;) {
      for (int i = 1000; i < 9000; ++i) {
        const auto t = (static_cast<double>(i) / 10000) * 6.28;
        // cr
        fractal_reg[6] = double_to_fix32_4(0.7885 * std::cos(t));
        // ci
        fractal_reg[7] = double_to_fix32_4(0.7885 * std::sin(t));

        ::usleep(10000);
      }
    }
  });

  redraw(&ctx, nullptr, 0);

  while (::wl_display_dispatch(ctx.display) != -1)
    ;

  ::wl_display_disconnect(ctx.display);
}
