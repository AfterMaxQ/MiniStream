add_library(ministream_core INTERFACE)
add_library(MiniStream::Core ALIAS ministream_core)

target_include_directories(
  ministream_core
  INTERFACE
    "${PROJECT_SOURCE_DIR}/src"
)

target_compile_features(ministream_core INTERFACE cxx_std_20)
