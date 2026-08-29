add_executable(ministream_host main.cpp)
target_link_libraries(ministream_host PRIVATE MiniStream::Core)
ministream_set_warnings(ministream_host)
