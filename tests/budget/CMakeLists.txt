# interp_budget_us's hold (src/game/budget.c, decisions D68, D74), driven
# with costs from the hardware logs: 60 fps where it fits, a steady 30 where
# it doesn't, and back from a hold within seconds.
add_executable(test_budget test_budget.c "${CMAKE_SOURCE_DIR}/src/game/budget.c")
target_compile_options(test_budget PRIVATE -m32 -O1 -g ${SPYRO_GUARD_FLAGS})
target_link_options(test_budget PRIVATE -m32 -no-pie)
add_test(NAME budget COMMAND test_budget)
