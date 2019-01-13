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
#pragma HLS INLINE
  return z.real() * z.real() + z.imag() * z.imag() > fix64_type{4.0};
}

template <std::uint8_t Iter>
void evaluate(std::uint8_t i0, std::complex<fix64_type> c, std::complex<fix64_type> z0,
              std::uint8_t& i, std::complex<fix64_type>& z) {
#pragma HLS ALLOCATION instances=mul limit=2 operation
  i = i0;
  z = z0;
loop_evaluate:
  for (std::uint8_t t = 0; t < Iter && !finished(z); ++t) {
    z = z * z + c;
    i = i + 1;
  }
}

video_type pack(std::uint32_t x, std::uint32_t y, std::uint8_t i) {
  auto p = video_type{};
  p.data = uint24_type{color_table[i]};
  p.user = x == 0 && y == 0;   // Start-of-Frame
  p.last = x == MAX_WIDTH - 1; // End-of-Line
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
#pragma HLS DATAFLOW
      const auto z0 = initialize_z(x, y, x1, y1, dx, dy, offset_x, offset_y);

      std::uint8_t i1, i2, i3, i4, i5, i6, i7, i8;
      std::complex<fix64_type> z1, z2, z3, z4, z5, z6, z7, z8;
      evaluate<(MAX_ITERATIONS + 1) / 8    >(0u, c, z0, i1, z1);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i1, c, z1, i2, z2);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i2, c, z2, i3, z3);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i3, c, z3, i4, z4);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i4, c, z4, i5, z5);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i5, c, z5, i6, z6);
      evaluate<(MAX_ITERATIONS + 1) / 8    >(i6, c, z6, i7, z7);
      evaluate<(MAX_ITERATIONS + 1) / 8 - 1>(i7, c, z7, i8, z8);

      const auto v = pack(x, y, i8);

      m_axis << v;
    }
  }
}
