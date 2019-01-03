#include <hls_opencv.h>
#include "fractal.h"

static constexpr auto OUTPUT_IMAGE = "out.ppm";

auto main() -> int {
  cv::Mat dst(MAX_HEIGHT, MAX_WIDTH, CV_8UC3);

  stream_type stream_out;
  fractal(stream_out);
  AXIvideo2cvMat(stream_out, dst);

  cv::imwrite(OUTPUT_IMAGE, dst);
}
