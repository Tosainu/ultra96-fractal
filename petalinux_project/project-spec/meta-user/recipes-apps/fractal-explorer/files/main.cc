#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string_view>
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

#include <linux/videodev2.h>
}

using namespace std::string_view_literals;

static constexpr std::uint32_t NUM_BUFFERS = 8;

struct window_context {
  int width;
  int height;

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
  int texture_id;
  std::unique_ptr<std::uint8_t[]> buffer;

  int video_fd;
  std::array<std::uint8_t*, NUM_BUFFERS> video_buffers;
};

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

  {
    ::v4l2_plane planes[VIDEO_MAX_PLANES];

    ::v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;
    if (::ioctl(ctx->video_fd, VIDIOC_DQBUF, &buf) == 0) {
      auto rgba = ctx->buffer.get();
      auto rgb = ctx->video_buffers[buf.index];
      const auto rgb_end = rgb + buf.m.planes[0].bytesused;

      const auto t1 = std::chrono::steady_clock::now();

      asm volatile(
          "mov  w0, #0xff \n\t"
          "dup  v3.8b, w0 \n\t"
          "1:   \n\t"
          "ld3  {v0.8b-v2.8b}, [%[src]] \n\t"
          "add  %[src], %[src], #24 \n\t"
          "cmp  %[end], %[src] \n\t"
          "st4  {v0.8b-v3.8b}, [%[dst]] \n\t"
          "add  %[dst], %[dst], #32 \n\t"
          "bhi  1b"
          : [src] "+r"(rgb), [dst] "+r"(rgba)
          : [end] "r"(rgb_end)
          : "w0", "v0", "v1", "v2", "v3");

      const auto t2 = std::chrono::steady_clock::now();
      const auto t1t2 = (t2 - t1) / std::chrono::nanoseconds(1);
      std::cout << std::setw(12) << t1t2 << std::endl;

      ::nvgUpdateImage(ctx->vg, ctx->texture_id, ctx->buffer.get());

      buf.length = 1;

      if (::ioctl(ctx->video_fd, VIDIOC_QBUF, &buf) == -1) {
        perror_exit("VIDIOC_QBUF");
      }
    } else {
      if (errno != EAGAIN) {
        perror_exit("VIDIOC_DQBUF");
      }
    }
  }

  ::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  ::glClear(GL_COLOR_BUFFER_BIT);

  const auto width = ctx->width;
  const auto height = ctx->height;

  ::nvgBeginFrame(ctx->vg, width, height, 1.0f);
  {
    ::NVGpaint img = ::nvgImagePattern(ctx->vg, 0, 0, width, height, 0, ctx->texture_id, 1.0f);
    ::nvgBeginPath(ctx->vg);
    ::nvgRect(ctx->vg, 0, 0, width, height);
    ::nvgFillPaint(ctx->vg, img);
    ::nvgFill(ctx->vg);

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

  ctx->redraw_cb = ::wl_surface_frame(ctx->surface);
  ::wl_callback_add_listener(ctx->redraw_cb, &listener, ctx);
}

auto main() -> int {
  window_context ctx{};
  ctx.width = 1920;
  ctx.height = 1080;

  ctx.video_fd = ::open("/dev/video0", O_RDWR | O_NONBLOCK);
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
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_RGB24;
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

      ctx.video_buffers.at(i) = static_cast<std::uint8_t*>(mem);
      std::printf("buffer%d @ %p, length: %u\n", i, mem, buf.m.planes[0].length);
    }

    for (auto i = 0u; i < req.count; ++i) {
      ::v4l2_plane planes[VIDEO_MAX_PLANES];
      ::v4l2_buffer buf{};
      buf.index = i;
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.length = 1;
      buf.m.planes = planes;
      if (::ioctl(ctx.video_fd, VIDIOC_QBUF, &buf) == -1) {
        perror_exit("VIDIOC_QBUF");
      }
    }
  }

  ::v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  if (::ioctl(ctx.video_fd, VIDIOC_STREAMON, &type) == -1) {
    perror_exit("VIDIOC_STREAMON");
  }

  // ------- Wayland -------

  ::wl_display* display = ::wl_display_connect(nullptr);
  if (!display) {
    std::cerr << "failed to connect display" << std::endl;
    return -1;
  }

  ::wl_registry* registry = ::wl_display_get_registry(display);

  ::wl_registry_add_listener(registry, &registry_listener, &ctx);

  ::wl_display_dispatch(display);
  ::wl_display_roundtrip(display);
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

  ctx.egl_display = ::eglGetDisplay(static_cast<::EGLNativeDisplayType>(display));
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

  ctx.vg = ::nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
  if (!ctx.vg) {
    std::cerr << "failed to initialize nanovg" << std::endl;
    return -1;
  }

  ::nvgCreateFont(ctx.vg, "mono", "/usr/share/fonts/ttf/LiberationMono-Regular.ttf");

  ctx.buffer = std::make_unique<std::uint8_t[]>(ctx.width * ctx.height * 4);
  ctx.texture_id = ::nvgCreateImageRGBA(ctx.vg, ctx.width, ctx.height, 0, ctx.buffer.get());

  redraw(&ctx, nullptr, 0);

  while (::wl_display_dispatch(display) != -1)
    ;

  ::wl_display_disconnect(display);
}
