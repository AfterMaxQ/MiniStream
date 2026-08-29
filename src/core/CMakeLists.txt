add_library(
  ministream_core
  STATIC
  protocol/wire.cpp
)
add_library(MiniStream::Core ALIAS ministream_core)

target_include_directories(
  ministream_core
  PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_compile_features(ministream_core PUBLIC cxx_std_20)
ministream_set_warnings(ministream_core)
