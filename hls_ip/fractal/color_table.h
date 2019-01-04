#ifndef COLOR_TABLE_H
#define COLOR_TABLE_H

#include <array>
#include <type_traits>

namespace detail {

template <std::size_t... Values>
struct index_sequence {};

template <std::size_t N, std::size_t... Values>
struct index_sequence_impl : index_sequence_impl<N - 1, N - 1, Values...> {};

template <std::size_t... Values>
struct index_sequence_impl<0, Values...> {
  using type = index_sequence<Values...>;
};

template <std::size_t N>
using make_index_sequence = typename index_sequence_impl<N>::type;

template <class Function, std::size_t... Indeces>
constexpr auto make_array_impl(Function f, index_sequence<Indeces...>)
    -> std::array<typename std::result_of<Function(std::size_t)>::type, sizeof...(Indeces)> {
  return {{f(Indeces)...}};
}

template <std::size_t N, class Function>
constexpr auto make_array(Function f)
    -> std::array<typename std::result_of<Function(std::size_t)>::type, N> {
  return make_array_impl(f, make_index_sequence<N>{});
}

template <class T>
constexpr T constexpr_min(T a, T b) {
  return a < b ? a : b;
}

constexpr std::uint32_t f2i(double d) {
  return static_cast<std::uint32_t>(constexpr_min(d, 1.0) * 255.0);
}

constexpr std::uint32_t colorize(double t) {
  return (detail::f2i(9.0 * (1.0 - t) * t * t * t) << 16) |          // blue
         (detail::f2i(15.0 * (1.0 - t) * (1.0 - t) * t * t) << 8) |  // green
         (detail::f2i(8.5 * (1.0 - t) * (1.0 - t) * (1.0 - t) * t)); // red
}

constexpr std::uint32_t index2color(std::size_t i) {
  return colorize(static_cast<double>(i) / 255.0);
}

} // namespace detail

constexpr std::array<std::uint32_t, 256> make_color_table() {
  return detail::make_array<256>(detail::index2color);
}

#endif // COLOR_TABLE_H
