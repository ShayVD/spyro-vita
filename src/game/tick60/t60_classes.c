/* The moby classes judged for 60 Hz (decisions D130; docs/tick60.md lists
 * each one's judgment: its per-tick sites as durations, speeds,
 * probabilities or sequence steps, and what it cost). A class listed here
 * runs every vblank in the pilot's levels (Artisans and its levels:
 * cmake/tick60.cmake's T60_LEVELS), with its sites marked in
 * patches/spyro-1-port/0014-tick60-artisans-mobys.patch. Any other class
 * runs the original's rules every other vblank (t60_rules.c). */
#include "tick60_rules.h"

static const unsigned char k_60[512] = {
    /* judged 60 Hz, with sites marked */
    [33] = 1, /* Egg Thief */
    [110] = 1, /* Dragon Pad Fairy */
    [115] = 1, /* Club Gnorc */
    [309] = 1, /* Large metal fragment */
    [339] = 1, /* Thief Gnorc */
    [392] = 1, /* Fan Chest Fan */
    [502] = 1, /* Chicken Feather */
    [17] = 1, /* Unused flight projectile */
    [114] = 1, /* Gnorc */
    [174] = 1, /* Locked Chest */
    [250] = 1, /* Crystal Dragon */
    [300] = 1, /* Whirlwind */
    [329] = 1, /* Spring Chest */
    [335] = 1, /* Sheepdog */
    [16] = 1, /* Butterfly */
    [23] = 1, /* Bull */
    [34] = 1, /* Dragon Egg */
    [49] = 1, /* Toasty Entrance Control */
    [121] = 1, /* Ram */
    [69] = 1, [151] = 1, [257] = 1, [425] = 1, [480] = 1, /* Chest and basket fragments */
    [349] = 1, /* Shepherd (Toasty) */
    [387] = 1, [388] = 1, [389] = 1, [393] = 1, [394] = 1, [396] = 1, [490] = 1, [491] = 1, /* Flight HUD icons */
    [395] = 1, /* Matador Gnorc */
    [76] = 1, [426] = 1, [427] = 1, [428] = 1, [429] = 1, [430] = 1, [431] = 1, [432] = 1, [433] = 1, [434] = 1, [435] = 1, [436] = 1, [437] = 1, [438] = 1, [439] = 1, [440] = 1, [441] = 1, [442] = 1, [443] = 1, [444] = 1, [445] = 1, [446] = 1, [447] = 1, [448] = 1, [449] = 1, [450] = 1, [451] = 1, /* Text letters */
    [10] = 1, /* Sheep fodder */
    [113] = 1, /* Shepherd */
    [120] = 1, /* Sparx */
    [165] = 1, /* Gnorc (Dark Hollow) */
    [362] = 1, /* Train wheel fragment */
    [390] = 1, /* Fan Chest */
    [391] = 1, /* Smoke particle spawner */
    /* judged 60 Hz, no sites: nothing per tick the automatic parts miss */
    [78] = 1, /* TNT Barrel Flights */
    [173] = 1, /* Key */
    [187] = 1, /* Artisans Balloonist */
    [234] = 1, /* Barrier effect */
    [323] = 1, /* Fodder Respawner */
    [413] = 1, /* Fodder Chicken */
    [13] = 1, /* Gem spawner */
    [18] = 1, /* Sunny Flight entrance stones */
    [166] = 1, /* Large Armoured Gnorc */
    [194] = 1, /* Basket */
    [331] = 1, [332] = 1, [333] = 1, /* Dragon pads */
    [350] = 1, /* Sunny Flight entrance control */
    [353] = 1, /* Flight arches */
    [363] = 1, [364] = 1, [400] = 1, [405] = 1, [477] = 1, [507] = 1, [508] = 1, /* Water bubbles and splashes */
    [11] = 1, /* Waterfall mist */
    [129] = 1, /* Lamp */
    [251] = 1, /* Crystal Dragon fragment */
    [299] = 1, /* Flight Chest */
    [358] = 1, /* Flight controller */
    [412] = 1, /* Fodder Frog */
    [1] = 1, /* Portal text */
    [9] = 1, /* Exit vortex */
    [157] = 1, /* Bonfire */
    [195] = 1, /* Metal Chest */
    [314] = 1, /* Toasty */
    [336] = 1, [342] = 1, /* Tulips */
    [416] = 1, /* Balloon */
    [421] = 1, /* Life Basket */
    /* left at 30 Hz: 286 ambient sound player (its path step), 308 flight
     * plane, 398 portal path moby, 407 train front, 408 train carriage
     * (func_8003BFC0, or a script Spyro's own 60 Hz side shares) */
};

int t60_class_60(int pClass) {
  return pClass >= 0 && pClass < (int)sizeof k_60 && k_60[pClass];
}
