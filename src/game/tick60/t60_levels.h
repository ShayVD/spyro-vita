/* The levels whose mobys the 60 Hz rules run (decisions D130, D142): every
 * overlay with a moby_update.c (cmake/tick60.cmake builds each one's a
 * second time). T60_LEVEL(name, pilot): pilot 1 for the pilot's levels
 * (Artisans and its five), where every class t60_classes.c judged for
 * 60 Hz runs every vblank; in the others only the classes judged for every
 * level do (Sparx), and the rest run the original's rules every other
 * vblank. Included by src/game/tick60.c (the game's UpdateMoby, 30 Hz side)
 * and src/game/tick60/t60_rules.c (the set's copies), each with its own
 * T60_LEVEL. */
T60_LEVEL(LEVEL_10, 1)
T60_LEVEL(LEVEL_11, 1)
T60_LEVEL(LEVEL_12, 1)
T60_LEVEL(LEVEL_13, 1)
T60_LEVEL(LEVEL_14, 1)
T60_LEVEL(LEVEL_15, 1)
T60_LEVEL(LEVEL_20, 0)
T60_LEVEL(LEVEL_21, 0)
T60_LEVEL(LEVEL_22, 0)
T60_LEVEL(LEVEL_23, 0)
T60_LEVEL(LEVEL_24, 0)
T60_LEVEL(LEVEL_25, 0)
T60_LEVEL(LEVEL_30, 0)
T60_LEVEL(LEVEL_31, 0)
T60_LEVEL(LEVEL_32, 0)
T60_LEVEL(LEVEL_33, 0)
T60_LEVEL(LEVEL_34, 0)
T60_LEVEL(LEVEL_35, 0)
T60_LEVEL(LEVEL_40, 0)
T60_LEVEL(LEVEL_41, 0)
T60_LEVEL(LEVEL_42, 0)
T60_LEVEL(LEVEL_43, 0)
T60_LEVEL(LEVEL_44, 0)
T60_LEVEL(LEVEL_45, 0)
T60_LEVEL(LEVEL_50, 0)
T60_LEVEL(LEVEL_51, 0)
T60_LEVEL(LEVEL_52, 0)
T60_LEVEL(LEVEL_53, 0)
T60_LEVEL(LEVEL_54, 0)
T60_LEVEL(LEVEL_55, 0)
T60_LEVEL(LEVEL_60, 0)
T60_LEVEL(LEVEL_61, 0)
T60_LEVEL(LEVEL_62, 0)
T60_LEVEL(LEVEL_63, 0)
T60_LEVEL(LEVEL_64, 0)
