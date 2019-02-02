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

video_type pack(std::uint32_t x, std::uint32_t y, std::uint8_t i) {
  auto p = video_type{};
  p.data = uint24_type{color_table[i]};
  p.user = x == 0 && y == 0;   // Start-of-Frame
  p.last = x == MAX_WIDTH - 1; // End-of-Line
  p.keep = -1;
  return p;
}

void fractal(stream_type& m_axis) {
#pragma HLS ALLOCATION instances=sub limit=8 operation
#pragma HLS ALLOCATION instances=add limit=48 operation
#pragma HLS ALLOCATION instances=mul limit=24 operation
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
    for (std::uint32_t x = 0; x < MAX_WIDTH; x += UNROLL_FACTOR) {
      std::uint8_t i[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=i complete dim=1
      std::complex<fix64_type> z[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=z complete dim=1
      bool d[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=d complete dim=1

    loop1:
      for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
#pragma HLS PIPELINE II=1
        i[w] = 0u;
        z[w] = initialize_z(x + w, y, x1, y1, dx, dy, offset_x, offset_y);
        d[w] = false;
      }

    loop2:
      for (std::uint8_t t = 0; t < MAX_ITERATIONS; ++t) {
#pragma HLS PIPELINE II=5
      loop2_1:
        for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
          const auto zr2 = z[w].real() * z[w].real();
          const auto zi2 = z[w].imag() * z[w].imag();
          const auto zri = z[w].real() * z[w].imag();

          d[w] = d[w] || (zr2 + zi2 > fix64_type{4.0});
          z[w] = d[w] ? z[w]
                      : std::complex<fix64_type>{zr2 - zi2 + c.real(), zri + zri + c.imag()};
          i[w] = d[w] ? i[w] : i[w] + 1;
        }

        bool b = d[0];
      loop2_2:
        for (std::uint32_t w = 1; w < UNROLL_FACTOR; w++) b = b && d[w];
        if (b) break;
      }

    loop3:
      for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
#pragma HLS PIPELINE II=1
        m_axis << pack(x + w, y, i[w]);
      }
    }
  }
}
