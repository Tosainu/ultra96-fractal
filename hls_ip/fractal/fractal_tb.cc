#include <algorithm>
#include <iostream>
#include <vector>

#include <hls_opencv.h>
#include "fractal.h"
#include "color_table.h"

static constexpr auto OUTPUT_IMAGE = "out.ppm";

cv::Mat fractal_cpu(std::uint32_t width, std::uint32_t height, fixed_type x1, fixed_type y1,
                    fixed_type dx, fixed_type dy, fixed_type offset_x, fixed_type offset_y,
                    fixed_type cr, fixed_type ci) {
  cv::Mat dst(height, width, CV_8UC3);

  const auto c = std::complex<fixed_type>{cr, ci};

  constexpr auto color_table = make_color_table();

  for (std::uint32_t y = 0; y < height; y++) {
    for (std::uint32_t x = 0; x < width; x++) {
      const auto cx = -x1 + dx * x + offset_x;
      const auto cy = -y1 + dy * y - offset_y;

      auto z = std::complex<fixed_type>{cx, cy};

      std::uint32_t i = 0;
      while (i < MAX_ITERATIONS && z.real() * z.real() + z.imag() * z.imag() <= 4.0) {
        z = z * z + c;
        i++;
      }

      auto rgb = color_table[i];
      auto& p  = dst.at<cv::Vec3b>(y, x);
      p[0]     = (rgb >> 8) & 0xff;  // B
      p[1]     = (rgb >> 0) & 0xff;  // G
      p[2]     = (rgb >> 16) & 0xff; // R
    }
  }

  return dst;
}

template <std::uint32_t N>
void split_stream(stream_type<N>& in, stream_type<1u>& out) {
  for (std::uint32_t i = 0; i < MAX_WIDTH * MAX_HEIGHT / N; ++i) {
    video_type<N> pp{};
    in >> pp;
    for (std::uint32_t j = 0; j < N; ++j) {
      video_type<1u> p{};
      p.data = pp.data((j + 1) * 24 - 1, j * 24);
      p.keep = -1;
      p.user = pp.user[j];
      p.last = j == (N - 1) && pp.last;
      out << p;
    }
  }
}

auto main() -> int {
  cv::Mat dst(MAX_HEIGHT, MAX_WIDTH, CV_8UC3);

  constexpr double ratio  = MAX_HEIGHT / MAX_WIDTH;
  constexpr double scale  = 1.0;
  constexpr auto x1       = 1.0 / scale;
  constexpr auto y1       = ratio / scale;
  constexpr auto dx       = 2.0 * x1 / MAX_WIDTH;
  constexpr auto dy       = 2.0 * y1 / MAX_HEIGHT;
  constexpr auto offset_x = 0.0;
  constexpr auto offset_y = 0.0;
  constexpr auto cr       = -0.4;
  constexpr auto ci       = 0.6;

  stream_type<PPC> stream_out;
  fractal(x1, y1, dx, dy, offset_x, offset_y, cr, ci, stream_out);
  stream_type<1u> stream_out2;
  split_stream<PPC>(stream_out, stream_out2);
  AXIvideo2cvMat(stream_out2, dst);

  // GBR2BGR
  auto channels = std::vector<cv::Mat>{};
  cv::split(dst, channels);
  std::swap(channels[0], channels[1]);
  cv::merge(channels, dst);

  const auto dst_cpu =
      fractal_cpu(MAX_WIDTH, MAX_HEIGHT, x1, y1, dx, dy, offset_x, offset_y, cr, ci);

  cv::Mat diff;
  cv::absdiff(dst, dst_cpu, diff);

  const auto sumdiff = cv::sum(diff);
  if (sumdiff[0] || sumdiff[1] || sumdiff[2]) {
    std::cerr << "dst != dst_cpu" << std::endl;
  } else {
    std::cout << "dst == dst_cpu" << std::endl;
  }

  cv::imwrite(OUTPUT_IMAGE, dst);
}
