cmake_minimum_required(VERSION 3.24)

project(fractal-explorer LANGUAGES CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(Cairo REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(GLESv2 REQUIRED IMPORTED_TARGET glesv2)

add_executable(fractal-explorer main.cc)
set_target_properties(fractal-explorer PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
target_compile_options(fractal-explorer PRIVATE
  -Wall
  -Wextra
  -pedantic
  $<$<AND:$<STREQUAL:${CMAKE_GENERATOR},Ninja>,$<CXX_COMPILER_ID:GNU>>:-fdiagnostics-color=always>
  $<$<AND:$<STREQUAL:${CMAKE_GENERATOR},Ninja>,$<CXX_COMPILER_ID:Clang>>:-fcolor-diagnostics>
)
target_link_libraries(fractal-explorer PRIVATE
  PkgConfig::Cairo
  PkgConfig::DRM
  PkgConfig::EGL
  PkgConfig::GBM
  PkgConfig::GLESv2
)
install(TARGETS fractal-explorer)
