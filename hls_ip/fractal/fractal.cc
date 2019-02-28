#include <cmath>
#include <complex>

#include "color_table.h"
#include "fractal.h"

static constexpr auto color_table = make_color_table();

inline ap_uint<BPP * 2> concat(pixel_type v1, pixel_type v2) {
#pragma HLS INLINE
  return v2, v1;
}

template <class... Ts>
inline ap_uint<BPP * (2 + sizeof...(Ts))> concat(pixel_type v1, pixel_type v2, Ts... vs) {
#pragma HLS INLINE
  return concat(v2, vs...), v1;
}

template <std::uint32_t N, std::size_t... Indeces>
inline ap_uint<BPP * N> pack_data_impl(std::uint8_t i[N], detail::index_sequence<Indeces...>) {
#pragma HLS INLINE
  return concat(pixel_type{color_table[i[Indeces]]}...);
}

template <std::uint32_t N>
inline ap_uint<BPP * N> pack_data(std::uint8_t i[N]) {
#pragma HLS INLINE
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

void fractal(fixed_type x0, fixed_type y0, fixed_type dx, fixed_type dy, fixed_type cr,
             fixed_type ci, stream_type<PPC>& m_axis) {
#pragma HLS ALLOCATION instances=mul limit=96 operation
#pragma HLS INTERFACE s_axilite bundle=ctrl port=return
#pragma HLS INTERFACE s_axilite bundle=ctrl port=x0
#pragma HLS INTERFACE s_axilite bundle=ctrl port=y0
#pragma HLS INTERFACE s_axilite bundle=ctrl port=dx
#pragma HLS INTERFACE s_axilite bundle=ctrl port=dy
#pragma HLS INTERFACE s_axilite bundle=ctrl port=cr
#pragma HLS INTERFACE s_axilite bundle=ctrl port=ci
#pragma HLS INTERFACE axis register both port=m_axis

  const auto c = std::complex<fixed_type>{cr, ci};

  using fixed_type2 = decltype(fixed_type{} * std::uint32_t{});

  fixed_type2 dxx;
  fixed_type2 dyy;
  fixed_type2 dxw[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=dxw complete dim=1

loop_dxw:
  for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
#pragma HLS UNROLL skip_exit_check
    dxw[w] = dx * w;
  }

loop_height:
  for (std::uint32_t y = 0; y < MAX_HEIGHT; y++) {
  loop_width:
    for (std::uint32_t x = 0; x < MAX_WIDTH; x += UNROLL_FACTOR) {
#pragma HLS LOOP_FLATTEN off

      bool d[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=d complete dim=1
      std::uint8_t i[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=i complete dim=1
      std::complex<fixed_type> z[UNROLL_FACTOR];
#pragma HLS ARRAY_PARTITION variable=z complete dim=1

      dxx = dx * x;
      dyy = dy * y;

    loop_iteration:
      for (std::uint8_t t = 0; t < MAX_ITERATIONS; ++t) {
#pragma HLS PIPELINE II=1

      loop1:
        for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
#pragma HLS UNROLL skip_exit_check
          d[w] = t != 0 ? d[w] : false;
          i[w] = t != 0 ? i[w] : 0u;
          z[w] = t != 0 ? z[w] : std::complex<fixed_type>{-x0 + dxx + dxw[w], -y0 + dyy};
        }

      loop2:
        for (std::uint32_t w = 0; w < UNROLL_FACTOR; w++) {
          const auto zr2 = z[w].real() * z[w].real();
          const auto zi2 = z[w].imag() * z[w].imag();
          const auto zri = z[w].real() * z[w].imag();

          d[w] = d[w] || (zr2 + zi2 > fixed_type{4.0});
          i[w] = d[w] ? i[w] : i[w] + 1;
          z[w] = d[w] ? z[w] : std::complex<fixed_type>{zr2 - zi2 + c.real(), zri + zri + c.imag()};
        }

      loop3:
        for (std::uint32_t p = 0; p < UNROLL_FACTOR; p += PPC) {
#pragma HLS UNROLL skip_exit_check
          if (t == MAX_ITERATIONS - 1) m_axis << pack<PPC>(x + p, y, i + p);
        }
      }
    }
  }
}
