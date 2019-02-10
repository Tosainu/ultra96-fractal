#ifndef FRACTAL_H
#define FRACTAL_H

#include <ap_axi_sdata.h>
#include <ap_fixed.h>
#include <ap_int.h>
#include <hls_stream.h>

#include <cstdint>

static constexpr std::uint32_t MAX_WIDTH     = 1920u;
static constexpr std::uint32_t MAX_HEIGHT    = 1080u;
static constexpr std::uint32_t UNROLL_FACTOR = 16u;
static constexpr std::uint8_t MAX_ITERATIONS = 255u;

template <std::uint32_t N>
using video_type = ap_axiu<24 * N, N, 1, 1>;

template <std::uint32_t N>
using stream_type = hls::stream<video_type<N>>;

using uint8_type  = ap_uint<8>;
using uint24_type = ap_uint<24>;
using fix64_type  = ap_fixed<64, 12>;

void fractal(fix64_type x1, fix64_type y1, fix64_type dx, fix64_type dy, fix64_type offset_x,
             fix64_type offset_y, fix64_type cr, fix64_type ci,
             stream_type<UNROLL_FACTOR>& m_axis);

#endif // FRACTAL_H
