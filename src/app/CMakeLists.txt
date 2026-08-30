add_library(
  ministream_app
  STATIC
  controlled/controlled_runtime.cpp
)

target_include_directories(ministream_app PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(ministream_app PUBLIC cxx_std_20)
target_link_libraries(ministream_app PUBLIC MiniStream::Core)
ministream_set_warnings(ministream_app)
