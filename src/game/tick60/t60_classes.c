/* The moby classes judged for 60 Hz (decisions D130; docs/tick60.md lists
 * each one's judgment: its per-tick sites as durations, speeds,
 * probabilities or sequence steps, and what it cost). A class listed here
 * runs every vblank in every level (D144: a class's body is the same in
 * every overlay, which differ only in the classes they keep), with its
 * sites marked in patches/spyro-1-port/0014-tick60-artisans-mobys.patch
 * (the pilot's, D130) and patches/spyro-1-tick60/ (the rest, D144). Any
 * other class runs the original's rules every other vblank (t60_rules.c). */
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
    /* judged for 60 Hz in D144: every other class with a body in
     * moby_update.c (patches/spyro-1-tick60/0001-tick60-all-mobys.patch;
     * docs/tick60.md lists each one's sites). 286, 308, 407 and 408, left
     * at 30 Hz in the pilot, run at 60 now that func_8003BFC0 has a 60 Hz
     * form (t60_rules.c t60_path_step) */
    [28] = 1, /* Misty Bog Gnorcs */
    [32] = 1, /* Laser effect for Large Gnorc in Terrace */
    [35] = 1, /* Gunner Gnorc */
    [36] = 1, /* Magic Crafters Mountain Goat Fodder */
    [37] = 1, /* Blowhard Storm Cloud */
    [38] = 1, /* Storm Wizard Lightning */
    [39] = 1, /* Blowhard Storm Cloud Lightning */
    [45 ... 46] = 1, /* Armored Horrors and Giant Pansies */
    [47] = 1, /* Gift projectile */
    [48] = 1, /* Metalhead */
    [50] = 1, /* Devil dog (big) */
    [51] = 1, /* Devil dog (small) */
    [52] = 1, /* Cupid */
    [53] = 1, /* Armored Turtle (big) */
    [54] = 1, /* Armored Turtle (small) */
    [55] = 1, /* Cupid Arrow */
    [56] = 1, /* Bat projectile (PROTOTYPE) */
    [57] = 1, /* Banana projectile */
    [61] = 1, /* Lofty Castle Fairy Cage */
    [62] = 1, /* Puffer Bird */
    [66] = 1, /* Snowman Gnorc */
    [70] = 1, /* Metalhead pole effect */
    [77] = 1, [168] = 1, /* Dream Weavers Cannon Effect / Gnasty Projectile Effect */
    [82] = 1, /* Metalhead Zap Projectile */
    [91] = 1, /* Dream Weavers Cannon */
    [92] = 1, /* Carrot-Topped Monk */
    [100] = 1, [102] = 1, /* Banana Boy / Banana Boy (armored) */
    [101] = 1, [170] = 1, /* Strongarms / Strongarms (Metalhead) */
    [65] = 1, /* Metalhead poles */
    [104] = 1, /* Dream Weavers Cannon target? */
    [67 ... 68] = 1, [93] = 1, [133] = 1, [215] = 1, [255 ... 256] = 1, [423 ... 424] = 1, [478 ... 479] = 1, /* Spring chest fragments / Metal fragment / Door fragment / Tin soldier fragment / Chest fragments / Life basket fragments / Flight chest fragments */
    [14 ... 15] = 1, [83 ... 87] = 1, /* Life statue and orb / Gems */
    [88] = 1, /* Lofty fairies */
    [109] = 1, /* High Caves fairies */
    [122] = 1, /* Lamp Fool */
    [124] = 1, /* Lamp Fool transition */
    [126] = 1, /* Timer Fool */
    [130] = 1, /* Slap-Happy Armored Monk */
    [134] = 1, /* Gnorc-Adier */
    [135] = 1, /* Gnorc-Adier grenade */
    [136] = 1, /* Door */
    [137] = 1, /* Hog */
    [142] = 1, [241] = 1, [227] = 1, /* Kiss Fairy / Gold Kiss Fairy / Kiss Fairy (High Caves) */
    [143] = 1, /* Gnasty Laser Explosion */
    [145] = 1, /* Machine Gunner */
    [146] = 1, /* Peacekeepers Tent */
    [147] = 1, /* Gnorc Commando */
    [148] = 1, /* Gnorc Survivalist */
    [150] = 1, /* Jacques */
    [152 ... 153] = 1, /* Fireworks Chest Fragments */
    [154] = 1, /* Barrel Dispenser */
    [155] = 1, /* Turtle Projectile */
    [156] = 1, /* Bullet Projectile */
    [159] = 1, /* Key Thief (Gnasty) */
    [160] = 1, /* Mushroom Fodder */
    [161] = 1, /* Gnasty Gnorc */
    [162] = 1, /* Boat Gnorc */
    [163] = 1, /* Copter Gnorc */
    [169] = 1, /* Gnasty Projectile Centre */
    [171] = 1, /* Twilight Harbor Bridge Crank */
    [176] = 1, /* Gnorc Survivalist Grenade */
    [177] = 1, /* Thief Rocket */
    [179] = 1, /* Key Thief (Gnasty's Loot) */
    [181] = 1, /* Thief Key */
    [184] = 1, /* Vulture-wielding Gnorc */
    [186] = 1, /* Musket Gnorc (Dry Canyon) */
    [188] = 1, /* Peacekeepers Balloonist */
    [189] = 1, /* Magic Crafters Balloonist */
    [190] = 1, /* Beast Makers Balloonist */
    [191] = 1, /* Dream Weavers Balloonist */
    [192] = 1, /* Gnorc Gnexus Balloonist */
    [193] = 1, /* Fodder (Rabbit - Peace Keepers) */
    [197] = 1, /* Musket Gnorc Projectile (Dry Canyon) */
    [198] = 1, /* Ice Gnorc */
    [199] = 1, /* Engineer */
    [200] = 1, /* Dockworker */
    [201] = 1, /* TNT Wrangler */
    [202] = 1, /* TNT Barrel */
    [203] = 1, /* Tin Soldiers */
    [204] = 1, /* Haunted Wizards */
    [205] = 1, /* Lamp On Pole */
    [206] = 1, [239] = 1, [242] = 1, /* Small Cactus / Tall Cactus / Round Cactus */
    [207] = 1, /* Gnorc Cove Barrel */
    [208] = 1, /* Gnorc Gnexus Dragon Head Opener */
    [209] = 1, /* Snowball Projectile */
    [210] = 1, /* Metalhead Pole Red Effect */
    [211 ... 212] = 1, /* Green and Purple Ski Gnorcs */
    [213] = 1, /* Fodder (Bat - Ice Cavern) */
    [214] = 1, [216] = 1, /* Blue Hat Gnorc (Peace Keepers) / Red Hat Gnorc (Peace Keepers) */
    [217] = 1, /* Gnasty's Loot True ending control */
    [218] = 1, /* Cauldron */
    [222 ... 224] = 1, /* Stone Hill Trees */
    [225] = 1, /* Cannon */
    [226] = 1, /* Pueblo */
    [229] = 1, /* Fat Lady */
    [230] = 1, /* Vulture */
    [231] = 1, /* Armoured Ice Gnorc */
    [232] = 1, /* Gnorc Copter Blades */
    [236] = 1, /* Fodder (Rat) */
    [237] = 1, /* Cannonball */
    [238] = 1, /* Fodder (Peace Keepers) */
    [253] = 1, /* Large Beast (Alpine Ridge) */
    [260 ... 269] = 1, /* Digits */
    [270] = 1, /* Druid (Magic Crafters, Alpine) */
    [271] = 1, /* Armored Druid */
    [283] = 1, /* Storm Wizard */
    [284] = 1, /* Armoured Druid (Wizard Peak) */
    [285] = 1, /* Ice Wizard (Wizard Peak) */
    [286] = 1, /* Ambient sound player */
    [288 ... 289] = 1, /* Bird Feathers */
    [293 ... 294] = 1, /* Tree Tops Green and Red Thieves */
    [295] = 1, /* Ice Wizard Projectile */
    [296] = 1, /* Gnorc Balloonist Balloon */
    [298] = 1, /* Lofty Gnorc Balloonist */
    [301] = 1, /* Ice Wizard (High Caves) */
    [302] = 1, /* Druid (High Caves) */
    [303] = 1, /* Tornado Wizard */
    [304] = 1, /* Tornado */
    [305] = 1, /* Metalback Spiders */
    [308] = 1, /* Flight plane */
    [310 ... 311] = 1, /* Small metal fragments */
    [312] = 1, /* Fireworks Chest */
    [315] = 1, /* Yellow Beast Snack (Alpine Ridge) */
    [318] = 1, /* Ice Wizard Projectile Sparks */
    [326] = 1, /* Ice Wizard */
    [328] = 1, /* Druid moveable geometry helper? */
    [340] = 1, /* Flight Ring */
    [343] = 1, /* Large Gnorc Weapon Effect */
    [345 ... 347] = 1, /* +1, +2, +3 flight model */
    [359 ... 361] = 1, /* Wood fragments */
    [370] = 1, /* Kamikaze Tribesmen */
    [371] = 1, /* Doctor Shemp */
    [372] = 1, /* Fat Lady (Doctor Shemp) */
    [375 ... 386] = 1, /* Arch fragments */
    [397] = 1, [399] = 1, /* Night/Icy Flight Lighthouse Light / Lit version */
    [401] = 1, /* Safe */
    [402] = 1, /* Fireworks Rocket */
    [403] = 1, /* Attack Frog */
    [407] = 1, /* Train Front */
    [408] = 1, /* Train Carriage */
    [414] = 1, /* "You're Toast Gnorc!" Spyro */
    [417] = 1, /* TODO: return when PK Cannon and Cannon ball Mobys are */
    [453] = 1, /* Beast Makers chicken 2 (duplicate of 466 added after July) */
    [454 ... 456] = 1, /* Breakable fragment */
    [458 ... 459] = 1, /* Cannonball fragment */
    [461] = 1, /* Electro Gnorcs */
    [466] = 1, /* Beast Makers chicken */
    [467] = 1, /* Big Laser Gnorc */
    [475] = 1, /* Small Bush */
    [476] = 1, /* Dragon-Eating Plant */
    [481 ... 483] = 1, /* Flight Train, Wagon and Plane Fragments */
    [487] = 1, /* Chicken Cage */
    [493] = 1, /* Cactus Charred Particles */
    [494] = 1, /* Zap Effect */
    [495] = 1, /* Blowhard */
    [497] = 1, /* Druid Blowhard */
    /* left at 30 Hz: 398 portal path moby, a script Spyro's own 60 Hz side
     * shares (its hold on him lasts the 30 Hz tick, t60_spyro_release,
     * D141; run every vblank it would aim him from his half-moved
     * position) */
};

int t60_class_60(int pClass) {
  return pClass >= 0 && pClass < (int)sizeof k_60 && k_60[pClass];
}
