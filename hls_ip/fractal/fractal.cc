#include <cmath>
#include <complex>

#include "color_table.h"
#include "fractal.h"

static constexpr auto color_table = make_color_table();

std::complex<fixed_type> initialize_z(std::uint32_t x, std::uint32_t y, fixed_type x1,
                                      fixed_type y1, fixed_type dx, fixed_type dy,
                                      fixed_type offset_x, fixed_type offset_y) {
#pragma HLS INLINE
  return std::complex<fixed_type>{-x1 + dx * x + offset_x, -y1 + dy * y + offset_y};
}

inline ap_uint<BPP * 2> concat(pixel_type v1, pixel_type v2) {
  return v2, v1;
}

template <class... Ts>
inline ap_uint<BPP * (2 + sizeof...(Ts))> concat(pixel_type v1, pixel_type v2, Ts... vs) {
  return concat(v2, vs...), v1;
}

template <std::uint32_t N, std::size_t... Indeces>
inline ap_uint<BPP * N> pack_data_impl(std::uint8_t i[N], detail::index_sequence<Indeces...>) {
  return concat(pixel_type{color_table[i[Indeces]]}...);
}

template <std::uint32_t N>
inline ap_uint<BPP * N> pack_data(std::uint8_t i[N]) {
  return pack_data_impl<N>(i, detail::make_index_sequence<N>{});
}

template <std::uint32_t N>
video_type<N> pack(std::uint32_t x, std::uint32_t y, std::uint8_t i[N]) {
#pragma HLS INLINE
  auto p = video_type<N>{};
  p.data = pack_data<N>(i);
  p.user = x == 0 && y == 0;             // Start-of-Frame
  p.last = (x + N - 1) == MAX_WIDTH - 1; // End-of-Line
  p.keep = -1;
  return p;
}

void fractal(fixed_type x1, fixed_type y1, fixed_type dx, fixed_type dy, fixed_type offset_x,
             fixed_type offset_y, fixed_type cr, fixed_type ci,
             stream_type<UNROLL_FACTOR>& m_axis) {
#pragma HLS ALLOCATION instances=sub limit=8 operation
#pragma HLS ALLOCATION instances=add limit=48 operation
#pragma HLS ALLOCATION instances=mul limit=24 operation
#pragma HLS INTERFACE s_axilite port=x1
#pragma HLS INTERFACE s_axilite port=y1
#pragma HLS INTERFACE s_axilite port=dx
#pragma HLS INTERFACE s_axilite port=dy
#pragma HLS INTERFACE s_axilite port=offset_x
#pragma HLS INTERFACE s_axilite port=offset_y
#pragma HLS INTERFACE s_axilite port=cr
#pragma HLS INTERFACE s_axilite port=ci
#pragma HLS INTERFACE axis register both port=m_axis
#pragma HLS INTERFACE s_axilite port=return

  const auto c = std::complex<fixed_type>{cr, ci};

loop_height:
  for (std::uint32_t y = 0; y < MAX_HEIGHT; y++) {
  loop_width:
    for (std::uint32_t x = 0; x < MAX_WIDTH; x += UNROLL_FACTOR) {
      std::uint8_t i[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=i complete dim=1
      std::complex<fixed_type> z[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=z complete dim=1
      bool d[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=d complete dim=1

    loop1:
      for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
#pragma HLS UNROLL skip_exit_check
        i[w] = 0u;
        z[w] = initialize_z(x + w, y, x1, y1, dx, dy, offset_x, offset_y);
        d[w] = false;
      }

    loop2:
      for (std::uint8_t t = 0; t < MAX_ITERATIONS; ++t) {
#pragma HLS PIPELINE II=2
      loop2_1:
        for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
          const auto zr2 = z[w].real() * z[w].real();
          const auto zi2 = z[w].imag() * z[w].imag();
          const auto zri = z[w].real() * z[w].imag();

          d[w] = d[w] || (zr2 + zi2 > fixed_type{4.0});
          z[w] = d[w] ? z[w] : std::complex<fixed_type>{zr2 - zi2 + c.real(), zri + zri + c.imag()};
          i[w] = d[w] ? i[w] : i[w] + 1;
        }
      }

      m_axis << pack<UNROLL_FACTOR>(x, y, i);
    }
  }
}
