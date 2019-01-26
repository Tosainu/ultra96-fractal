#ifndef FRACTAL_H
#define FRACTAL_H

#include <ap_axi_sdata.h>
#include <ap_fixed.h>
#include <ap_int.h>
#include <hls_stream.h>

#include <cstdint>

static constexpr std::uint32_t MAX_WIDTH     = 1920u;
static constexpr std::uint32_t MAX_HEIGHT    = 1080u;
static constexpr std::uint32_t UNROLL_FACTOR = 32u;
static constexpr std::uint8_t MAX_ITERATIONS = 255u;

using video_type  = ap_axiu<24, 1, 1, 1>;
using stream_type = hls::stream<video_type>;
using uint8_type  = ap_uint<8>;
using uint24_type = ap_uint<24>;
using fix64_type  = ap_fixed<64, 12>;

void fractal(stream_type& m_axis);

#endif // FRACTAL_H
