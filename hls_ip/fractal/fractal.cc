#include <cmath>
#include <complex>

#include "color_table.h"
#include "fractal.h"

static constexpr auto color_table = make_color_table();

uint24_type colorize(std::uint32_t i) {
  return static_cast<uint24_type>(color_table[i]);
}

void fractal(stream_type& m_axis) {
#pragma HLS INTERFACE axis register both port=m_axis
#pragma HLS INTERFACE s_axilite port=return

  const auto x1 = fix64_type{1.0};
  const auto y1 = fix64_type{MAX_HEIGHT} / fix64_type{MAX_WIDTH};
  const auto dx = fix64_type{2.0} * x1 / fix64_type{MAX_WIDTH};
  const auto dy = fix64_type{2.0} * y1 / fix64_type{MAX_HEIGHT};

  const auto offset_x = fix64_type{0};
  const auto offset_y = fix64_type{0};

  video_type video;

  for (std::uint32_t y = 0; y < MAX_HEIGHT; y++) {
    for (std::uint32_t x = 0; x < MAX_WIDTH; x++) {
      // Set Start-of-Frame (tuser) and End-of-Line (tlast) singale
      // https://forums.xilinx.com/t5/Video/Video-Beginner-Series-14-Creating-a-Pattern-Generator-using-HLS/m-p/895489/highlight/true#M21986
      if (x == 0 && y == 0) {
        video.user = 1;
      } else {
        video.user = 0;
      }
      if (x == MAX_WIDTH - 1) {
        video.last = 1;
      } else {
        video.last = 0;
      }

      const auto cx = -x1 + dx * x + offset_x;
      const auto cy = -y1 + dy * y + offset_y;

      auto z       = std::complex<fix64_type>{cx, cy};
      const auto c = std::complex<fix64_type>{-0.4, 0.6};

      std::uint32_t i = 0;
      while (i < MAX_ITERATIONS && z.real() * z.real() + z.imag() * z.imag() <= 4.0) {
        z = z * z + c;
        i++;
      }

      video.data = colorize(i);

      m_axis << video;
    }
  }
}
