#include <algorithm>
#include <iostream>
#include <vector>

#include <hls_opencv.h>
#include "fractal.h"
#include "color_table.h"

static constexpr auto OUTPUT_IMAGE = "out.ppm";

cv::Mat fractal_cpu(std::uint32_t width, std::uint32_t height) {
  cv::Mat dst(height, width, CV_8UC3);

  const auto x1 = fix64_type{1.0};
  const auto y1 = fix64_type{width} / fix64_type{height};
  const auto dx = fix64_type{2.0} * x1 / fix64_type{MAX_WIDTH};
  const auto dy = fix64_type{2.0} * y1 / fix64_type{MAX_HEIGHT};

  const auto offset_x = fix64_type{0};
  const auto offset_y = fix64_type{0};

  const auto c = std::complex<fix64_type>{-0.4, 0.6};

  constexpr auto color_table = make_color_table();

  for (std::uint32_t y = 0; y < height; y++) {
    for (std::uint32_t x = 0; x < width; x++) {
      const auto cx = -x1 + dx * x + offset_x;
      const auto cy = -y1 + dy * y + offset_y;

      auto z = std::complex<fix64_type>{cx, cy};

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

auto main() -> int {
  cv::Mat dst(MAX_HEIGHT, MAX_WIDTH, CV_8UC3);

  stream_type stream_out;
  fractal(stream_out);
  AXIvideo2cvMat(stream_out, dst);

  // GBR2BGR
  auto channels = std::vector<cv::Mat>{};
  cv::split(dst, channels);
  std::swap(channels[0], channels[1]);
  cv::merge(channels, dst);

  const auto dst_cpu = fractal_cpu(MAX_WIDTH, MAX_HEIGHT);

  const auto diff = cv::sum(dst - dst_cpu);
  if (diff[0] || diff[1] || diff[2]) {
    std::cerr << "dst != dst_cpu" << std::endl;
  } else {
    std::cout << "dst == dst_cpu" << std::endl;
  }

  cv::imwrite(OUTPUT_IMAGE, dst);
}
