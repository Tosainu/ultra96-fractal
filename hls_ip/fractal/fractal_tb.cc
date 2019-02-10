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
      const auto cy = -y1 + dy * y + offset_y;

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

  const double ratio  = MAX_HEIGHT / MAX_WIDTH;
  const double scale  = 1.0;
  const auto x1       = fixed_type{1.0 / scale};
  const auto y1       = fixed_type{ratio / scale};
  const auto dx       = fixed_type{2 * x1 / MAX_WIDTH};
  const auto dy       = fixed_type{2 * y1 / MAX_HEIGHT};
  const auto offset_x = fixed_type{0.0};
  const auto offset_y = fixed_type{0.0};
  const auto cr       = fixed_type{-0.4};
  const auto ci       = fixed_type{0.6};

  stream_type<UNROLL_FACTOR> stream_out;
  fractal(x1, y1, dx, dy, offset_x, offset_y, cr, ci, stream_out);
  stream_type<1u> stream_out2;
  split_stream<UNROLL_FACTOR>(stream_out, stream_out2);
  AXIvideo2cvMat(stream_out2, dst);

  // GBR2BGR
  auto channels = std::vector<cv::Mat>{};
  cv::split(dst, channels);
  std::swap(channels[0], channels[1]);
  cv::merge(channels, dst);

  const auto dst_cpu =
      fractal_cpu(MAX_WIDTH, MAX_HEIGHT, x1, y1, dx, dy, offset_x, offset_y, cr, ci);

  const auto diff = cv::sum(dst - dst_cpu);
  if (diff[0] || diff[1] || diff[2]) {
    std::cerr << "dst != dst_cpu" << std::endl;
  } else {
    std::cout << "dst == dst_cpu" << std::endl;
  }

  cv::imwrite(OUTPUT_IMAGE, dst);
}
