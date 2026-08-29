add_executable(ministream_host main.cpp)
target_link_libraries(ministream_host PRIVATE MiniStream::Core)
ministream_set_warnings(ministream_host)

add_custom_command(
  TARGET ministream_host
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:ministream_sodium>
          $<TARGET_FILE_DIR:ministream_host>
)
