#include <cmath>
#include <complex>

#include "color_table.h"
#include "fractal.h"

static constexpr auto color_table = make_color_table();

std::complex<fix64_type> initialize_z(std::uint32_t x, std::uint32_t y, fix64_type x1,
                                      fix64_type y1, fix64_type dx, fix64_type dy,
                                      fix64_type offset_x, fix64_type offset_y) {
  return std::complex<fix64_type>{-x1 + dx * x + offset_x, -y1 + dy * y + offset_y};
}

bool finished(std::complex<fix64_type> z) {
  return z.real() * z.real() + z.imag() * z.imag() > fix64_type{4.0};
}

template <std::uint32_t Iter>
void evaluate(std::uint32_t i0, std::complex<fix64_type> c, std::complex<fix64_type> z0,
              std::uint32_t& i, std::complex<fix64_type>& z) {
  i = i0;
  z = z0;
loop_evaluate:
  for (std::uint32_t t = 0; t < Iter && !finished(z); ++t) {
    z = z * z + c;
    i = i + 1;
  }
}

uint24_type to_rgb(std::uint32_t i) {
  return static_cast<uint24_type>(color_table[i]);
}

// Set Start-of-Frame (tuser) and End-of-Line (tlast) singale
// https://forums.xilinx.com/t5/Video/Video-Beginner-Series-14-Creating-a-Pattern-Generator-using-HLS/m-p/895489/highlight/true#M21986
ap_uint<1> tuser(std::uint32_t x, std::uint32_t y) {
  return x == 0 && y == 0;
}
ap_uint<1> tlast(std::uint32_t x) {
  return x == MAX_WIDTH - 1;
}

video_type make_video(uint24_type data, ap_uint<1> user, ap_uint<1> last) {
  auto p = video_type{};
  p.data = data;
  p.user = user;
  p.last = last;
  p.keep = -1;
  return p;
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

  const auto c = std::complex<fix64_type>{-0.4, 0.6};

loop_height:
  for (std::uint32_t y = 0; y < MAX_HEIGHT; y++) {
  loop_width:
    for (std::uint32_t x = 0; x < MAX_WIDTH; x++) {
      const auto z0 = initialize_z(x, y, x1, y1, dx, dy, offset_x, offset_y);

      std::uint32_t i1, i2, i3, i4, i5, i6, i7, i8;
      std::complex<fix64_type> z1, z2, z3, z4, z5, z6, z7, z8;
      evaluate<(MAX_ITERATIONS + 1) / 8    >(0u, c, z0, i1, z1);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i1, c, z1, i2, z2);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i2, c, z2, i3, z3);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i3, c, z3, i4, z4);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i4, c, z4, i5, z5);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i5, c, z5, i6, z6);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i6, c, z6, i7, z7);
      evaluate<(MAX_ITERATIONS + 1) / 8 - 1>(i7, c, z7, i8, z8);

      const auto d = to_rgb(i8);
      const auto u = tuser(x, y);
      const auto l = tlast(x);
      const auto v = make_video(d, u, l);

      m_axis << v;
    }
  }
}
