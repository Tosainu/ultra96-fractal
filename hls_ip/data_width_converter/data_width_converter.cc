#include "../fractal/fractal.h"

static constexpr std::uint32_t SPLIT = 8u;
static constexpr std::uint32_t PPC_IN = PPC;
static constexpr std::uint32_t PPC_OUT = PPC / SPLIT;

void data_width_converter(stream_type<PPC_IN>& s_axis, stream_type<PPC_OUT>& m_axis) {
#pragma HLS INTERFACE axis register both port=s_axis
#pragma HLS INTERFACE axis register both port=m_axis
#pragma HLS INTERFACE ap_ctrl_none port=return

  video_type<PPC_IN> data_in;
  s_axis >> data_in;

  for (std::uint32_t i = 0; i < SPLIT; ++i) {
#pragma HLS UNROLL skip_exit_check
    video_type<PPC_OUT> data_out;
    data_out.data = data_in.data((PPC_OUT * BPP) * (i + 1) - 1, (PPC_OUT * BPP) * i);
    data_out.user = i == 0 && data_in.user;
    data_out.last = i == (SPLIT - 1) && data_in.last;
    data_out.keep = -1;
    m_axis << data_out;
  }
}
