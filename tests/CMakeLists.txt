# One subdirectory per test group; each has its own CMakeLists.txt.
file(GLOB test_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(d ${test_dirs})
  if(IS_DIRECTORY "${d}" AND EXISTS "${d}/CMakeLists.txt")
    add_subdirectory("${d}")
  endif()
endforeach()
