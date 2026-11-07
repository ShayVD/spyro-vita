# The OT walk (src/platform/common/gpu_walk.c, stage 8d, D117) against
# PsyCross's, the reference library's: random packets of every GP0 kind
# through both walks (spyro --walk-check-synth; it needs no game data and
# runs nothing else). The mutants (gpu_walk.c's WALK_MUTATE) must fail.
# Only where the game links the reference walk (the arena build).
if(NOT SPYRO_NATIVE_MEM)
  set(walk_args --disc "${CMAKE_CURRENT_BINARY_DIR}/no-disc" --walk-check-synth 100000)
  add_test(NAME walk_synth COMMAND $<TARGET_FILE:spyro> ${walk_args}
    --log "${CMAKE_CURRENT_BINARY_DIR}/walk_synth.log")
  foreach(m 1 2)
    add_test(NAME walk_synth_mutant${m} COMMAND $<TARGET_FILE:spyro> ${walk_args}
      --log "${CMAKE_CURRENT_BINARY_DIR}/walk_synth_mutant${m}.log")
    set_tests_properties(walk_synth_mutant${m} PROPERTIES ENVIRONMENT WALK_MUTATE=${m} WILL_FAIL TRUE)
  endforeach()
endif()
