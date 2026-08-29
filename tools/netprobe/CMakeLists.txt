add_executable(
  ministream-netprobe
  main.cpp
  options.cpp
  report.cpp
)
target_include_directories(ministream-netprobe PRIVATE "${PROJECT_SOURCE_DIR}/tools")
target_link_libraries(ministream-netprobe PRIVATE MiniStream::Core)
ministream_set_warnings(ministream-netprobe)

if(WIN32)
  add_custom_command(
    TARGET ministream-netprobe
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ministream_sodium>
            $<TARGET_FILE_DIR:ministream-netprobe>
  )
endif()
