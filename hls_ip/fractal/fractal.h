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
static constexpr std::uint32_t PPC           = 32u;
static constexpr std::uint32_t BPP           = 24u;
static constexpr std::uint8_t MAX_ITERATIONS = 255u;

template <std::uint32_t N>
using video_type = ap_axiu<BPP * N, 1, 1, 1>;

template <std::uint32_t N>
using stream_type = hls::stream<video_type<N>>;

using pixel_type = ap_uint<BPP>;
using fixed_type = ap_fixed<32, 4>;

void fractal(fixed_type x0, fixed_type y0, fixed_type dx, fixed_type dy, fixed_type cr,
             fixed_type ci, stream_type<PPC>& m_axis);

#endif // FRACTAL_H
