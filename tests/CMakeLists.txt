# One subdirectory per test group; each has its own CMakeLists.txt.

# A mutant test runs a build (or a run) with one deliberate error, which the
# check must catch. It passes only when its output shows that error (REGEX,
# matched across lines; no ';' in it), whatever the exit code. WILL_FAIL
# passed on any failure, including a binary that never ran (no execute bit:
# qemu-i386 refuses it; issue #163), so the check could not fail.
function(spyro_mutant_test name regex)
  set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()

file(GLOB test_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(d ${test_dirs})
  if(IS_DIRECTORY "${d}" AND EXISTS "${d}/CMakeLists.txt")
    add_subdirectory("${d}")
  endif()
endforeach()
