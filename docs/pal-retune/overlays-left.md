# Overlay timing sites left after R3 (D137)

**Done (D146):** every site below, and PAL's inserted code around them, is ported. 249 of the WAD's 253 overlay functions are strictly PAL's, and no constant differs outside the credits overlay. What's left, scheduling only, is in D146. The list is kept as D137's record; its line numbers are the tree before D146.


`REGION=pal` build against the PAL disc (`build_matching.sh`'s `compare-wad-strict.txt`), the sites whose PAL/US ratio is a timing one, in R3's three sources: **542 site instances on 154 lines**. Each needs reading (`retune.py context`): the literal isn't on or next to the line (a macro, a value computed from others, a register the compiler kept), or PAL changed it in some levels and not others. Lines are the patched tree's (`build/s1-matching-pal/src/overlays/`, the decomp's line plus the one `#include "region.h"`).

Outside R3's sources (R2's): `balloonist.c` 70, `titlescreen.c` 14; 23 with no line.

| File:line | Statement | US -> PAL (count) | Levels |
|---|---|---|---|
| `moby_spawn.c:36` | `switch (pClass) {` | 312->370 (5), 327->277 (3), 277->327 (2), 309->365 (2) | 13, 15, 30, 32, 33, 43, 44, 50 … |
| `moby_spawn.c:1357` | `if (pParent->m_Class == 176) {` | -150->-179 (4) | 20, 21, 53, 62 |
| `moby_spawn.c:1358` | `props->m_Velocity.x = RandRange(-100, 100);` | -100->-119 (4), 100->120 (4) | 20, 21, 53, 62 |
| `moby_spawn.c:1359` | `props->m_Velocity.y = RandRange(-100, 100);` | -100->-119 (4), 100->120 (4) | 20, 21, 53, 62 |
| `moby_spawn.c:1362` | `props->m_Velocity.x = RandRange(-150, 150);` | 150->180 (4) | 20, 21, 53, 62 |
| `moby_spawn.c:1363` | `props->m_Velocity.y = RandRange(-150, 150);` | -150->-179 (4), 150->180 (4) | 20, 21, 53, 62 |
| `moby_update.c:100` | `g_AnimationFinished = moby->m_AnimationState.m_AnimationFlags & 2;` | 2->1 (1) | 12 |
| `moby_update.c:101` | `g_AnimFrameFinished = moby->m_AnimationState.m_AnimationFlags & 1;` | 1->2 (1) | 11 |
| `moby_update.c:104` | `switch (moby->m_Class) {` | 34->49 (1), 350->300 (1), 286->339 (1), 76->110 (1) | 10, 12, 13, 14, 24, 25, 35, 42 … |
| `moby_update.c:261` | `switch (moby->m_State) {` | 2->3 (1) | 11 |
| `moby_update.c:521` | `} else if (moby->m_Substate != 0 \|\| parent->m_State >= 0x80) {` | 9->8 (24) | 20, 21, 24, 31, 40, 43, 44, 50 … |
| `moby_update.c:522` | `spawnerProps->m_0x1c = 9;` | 9->8 (12) | 12, 14, 22, 30, 32, 34, 41, 51 … |
| `moby_update.c:658` | `func_80038638(moby, &g_Spyro.m_Position, 800,` | 130->156 (35), 14->17 (35) | 10, 11, 12, 13, 14, 15, 20, 21 … |
| `moby_update.c:824` | `switch (props->m_0x34) {` | 100->120 (1) | 13 |
| `moby_update.c:881` | `if (props->m_0x20 == 0 && DISTANCE_TO_SPYRO(moby) < 0x800 &&` | 120->100 (1) | 13 |
| `moby_update.c:1035` | `if (props->m_0x20 == 0 && DISTANCE_TO_SPYRO(moby) < 0x800 &&` | 120->100 (1) | 13 |
| `moby_update.c:1122` | `func_80039E94(moby, props->m_0x00, 0x100, RT(0xA0, 0xC0), 0, RT(8, 10)` | 256->307 (1) | 42 |
| `moby_update.c:1236` | `result = MobyMoveWithGravity(moby, &props->m_0x08, props->m_0x10,` | 12->15 (1) | 42 |
| `moby_update.c:1334` | `if ((moby->m_DamageFlags &` | 150->180 (9) | 11, 13, 20, 21, 22, 30, 31, 32 … |
| `moby_update.c:1626` | `if (OctDistance(&moby->m_Position, &PATH_NODE_POS(props->m_0x00, 0)) >` | 130->156 (4) | 22, 30, 32, 33 |
| `moby_update.c:1628` | `func_80039398(moby, 0x82, 0, 0, 1);` | 130->156 (4) | 13, 20, 21, 31 |
| `moby_update.c:1677` | `if (func_80039E94(moby, props->m_0x00, 0x100, RT(0xF0, 0x120), 0, 8, 0` | 256->307 (9), 8->10 (8) | 11, 13, 20, 21, 22, 30, 31, 32 … |
| `moby_update.c:1700` | `if (func_80039E94(moby, props->m_0x00, 0x100, RT(0xDA, 0x106), 0, RT(0` | 256->307 (9) | 11, 13, 20, 21, 22, 30, 31, 32 … |
| `moby_update.c:1811` | `result = func_80039E94(moby, props->m_0x00, 0x100, props->m_0x14, 0, 8` | 8->10 (8) | 11, 13, 20, 21, 22, 30, 31, 32 |
| `moby_update.c:1834` | `if (func_80039E94(moby, props->m_0x00, 0x100, props->m_0x14, 0, 8, 0x2` | 256->307 (9), 8->10 (8) | 11, 13, 20, 21, 22, 30, 31, 32 … |
| `moby_update.c:1858` | `if (++props->m_0x00->m_CurrentNode == 0xD) {` | 150->180 (9) | 11, 13, 20, 21, 22, 30, 31, 32 … |
| `moby_update.c:1929` | `if (func_80039E94(moby, props->m_0x00, 0x100, props->m_0x14, 0, 8, 0x2` | 256->307 (9), 8->10 (8) | 11, 13, 20, 21, 22, 30, 31, 32 … |
| `moby_update.c:2263` | `for (i = 0; i < 2; i++) {` | 30->25 (1) | 41 |
| `moby_update.c:2310` | `if (func_80039E94(moby, props->m_0x1c, 0x100, RT(0x5A, 0x6C), 0, RT(8,` | 256->307 (1) | 41 |
| `moby_update.c:2382` | `if (g_LevelMobys[props->m_0x28].m_State >= 0x80 \|\|` | 60->50 (1) | 41 |
| `moby_update.c:2824` | `MobyMoveWithGravity(moby, &props->m_0x08, props->m_0x14, &props->m_0x0` | 12->15 (1) | 54 |
| `moby_update.c:3198` | `if (moby->m_AnimationState.m_NextFrame > 4 &&` | 16->13 (1) | 44 |
| `moby_update.c:3246` | `if (moby->m_AnimationState.m_NextFrame > 3 &&` | 5->4 (1) | 44 |
| `moby_update.c:3319` | `func_80039E94(moby, props->m_0x04, 0x100, RT(0x78, 0x90), 0, 3, 5, 0xF` | 256->307 (1) | 44 |
| `moby_update.c:4035` | `MobyMoveWithGravity(moby, &props->m_0x00, props->m_0x08, &props->m_0x0` | 12->15 (1) | 51 |
| `moby_update.c:4186` | `if (moby->m_Substate == 2) {` | -150->-179 (2) | 43, 44 |
| `moby_update.c:4192` | `fragmentProps->m_0x00 = RandRange(-150, 150);` | 150->180 (2) | 43, 44 |
| `moby_update.c:4193` | `fragmentProps->m_0x02 = RandRange(-150, 150);` | -150->-179 (2), 150->180 (2) | 43, 44 |
| `moby_update.c:5215` | `if (!(props->m_0x10 == 1 &&` | 200->240 (2) | 50 |
| `moby_update.c:5327` | `if (func_80039E94(moby, props->m_0x30, 0x100, RT(0x8C, 0xA8), 0, RT(8,` | 256->307 (1) | 50 |
| `moby_update.c:5361` | `if (distance < 2500 &&` | 18->15 (1), 21->18 (1) | 50 |
| `moby_update.c:5401` | `result = MobyMoveWithGravity(moby, &props->m_0x00, props->m_0x04,` | 12->15 (1) | 50 |
| `moby_update.c:5404` | `if (result == 3 && moby->m_AnimationState.m_NextFrame >= RT(11, 9) &&` | 11->8 (1) | 50 |
| `moby_update.c:5417` | `} else {` | 20->17 (1) | 50 |
| `moby_update.c:5479` | `func_80038638(moby, &linkedMoby->m_Position, 1500, result, 4, RT(0x32,` | 14->17 (1) | 50 |
| `moby_update.c:5514` | `if (moby->m_DamageFlags == MOBY_DAMAGE_CHARGE) {` | 280->336 (2) | 43, 44 |
| `moby_update.c:5685` | `if (moby->m_AnimationState.m_NextFrame > 2 &&` | 5->4 (2) | 43, 44 |
| `moby_update.c:5740` | `result = func_80039E94(moby, props->m_0x24, 0x100, RT(0x8C, 0xA8), 0, ` | 256->307 (2) | 43, 44 |
| `moby_update.c:5912` | `result = func_80039E94(moby, props->m_0x24, 0x100, RT(0x78, 0x90), 0, ` | 256->307 (2) | 43, 44 |
| `moby_update.c:6139` | `if (moby->m_AnimationState.m_NextFrame > 5 &&` | -6->-5 (2) | 43, 44 |
| `moby_update.c:6342` | `result = func_80039E94(moby, props->m_0x38, 0x100, RT(0x78, 0x90), 0, ` | 256->307 (2) | 43, 44 |
| `moby_update.c:6772` | `if (DISTANCE_TO_SPYRO(moby) < 3800 &&` | 100->120 (1) | 11 |
| `moby_update.c:8814` | `MobyMoveWithGravity(moby, &props->m_0x10, props->m_0x0c, &props->m_0x1` | 12->15 (1) | 10 |
| `moby_update.c:8827` | `MobyMoveWithGravity(moby, &props->m_0x10, props->m_0x0c, &props->m_0x1` | 12->15 (1) | 10 |
| `moby_update.c:8885` | `if (DISTANCE_TO_SPYRO(moby) < 0x2000 &&` | 72->87 (2) | 12 |
| `moby_update.c:9206` | `if (dist < 512) {` | 310->370 (35) | 10, 11, 12, 13, 14, 15, 20, 21 … |
| `moby_update.c:9905` | `if (func_80039E94(moby, props->m_0x30, 0x100, RT(0x8C, 0xA8), 0, RT(8,` | 256->307 (1) | 50 |
| `moby_update.c:9940` | `if (distance < 0x6A4 &&` | 14->12 (1), 16->13 (1) | 50 |
| `moby_update.c:10057` | `func_80038638(moby, &linkedMoby->m_Position, 1500, result, 4, RT(0x32,` | 14->17 (1) | 50 |
| `moby_update.c:10154` | `if (func_80039E94(moby, props->m_0x34, 0x100, RT(0x8C, 0xA8), 0, RT(8,` | 256->307 (1) | 53 |
| `moby_update.c:10324` | `for (i = 0; i < 18; i++) {` | -90->-107 (1) | 53 |
| `moby_update.c:10325` | `particleVelocity.x = RandRange(-90, 90);` | -90->-107 (1), 90->108 (1) | 53 |
| `moby_update.c:10326` | `particleVelocity.y = RandRange(-90, 90);` | -90->-107 (1), 90->108 (1) | 53 |
| `moby_update.c:10392` | `if (variant == 1) {` | 100->120 (1) | 53 |
| `moby_update.c:10520` | `switch (props->m_0x00) {` | 160->192 (2) | 40, 42 |
| `moby_update.c:10672` | `if (props->m_0x5c != 0) {` | 350->420 (2) | 40, 42 |
| `moby_update.c:10857` | `if (SPYRO_BASE_Z_DISTANCE(moby) < 2000 &&` | 75->63 (2) | 32, 53 |
| `moby_update.c:10965` | `} else if (props->m_0x20 == 2) {` | 900->750 (2) | 32, 53 |
| `moby_update.c:10968` | `} else {` | 780->650 (2) | 32, 53 |
| `moby_update.c:11011` | `} else if (heightDifference > 0) {` | -50->-59 (2), 51->61 (2) | 32, 53 |
| `moby_update.c:11178` | `if (func_80039E94(moby, props->m_0x1c, 0x100, RT(0x8C, 0xA8), 0, RT(8,` | 256->307 (1) | 62 |
| `moby_update.c:11192` | `if (moby->m_AnimationState.m_NextFrame > RT(15, 12) &&` | 13->9 (1) | 62 |
| `moby_update.c:11201` | `velocity.x += RandRange(-0x18, 0x18);` | -24->-28 (1), 24->29 (1) | 62 |
| `moby_update.c:11202` | `velocity.y += RandRange(-0x18, 0x18);` | -24->-28 (1), 24->29 (1) | 62 |
| `moby_update.c:11222` | `MobyMoveWithGravity(moby, &props->m_0x04, props->m_0x08, &props->m_0x0` | 12->15 (1) | 62 |
| `moby_update.c:11370` | `if (func_80039E94(moby, props->m_0x04, 0x100, RT(0xA0, 0xC0), 0, RT(8,` | 256->307 (1) | 62 |
| `moby_update.c:11589` | `if (func_80039E94(moby, props->m_0x2c, 0x100, RT(170, 204), 0, RT(8, 1` | 256->307 (1) | 62 |
| `moby_update.c:11608` | `MobyMoveWithGravity(moby, &props->m_0x04, props->m_0x08, &props->m_0x0` | 12->15 (1) | 62 |
| `moby_update.c:11744` | `func_800529E4(moby, UPDATE_PROP_ROTMATRIX);` | 4->5 (1) | 54 |
| `moby_update.c:12004` | `for (i = 0; i < 16; i++) {` | -90->-107 (1) | 51 |
| `moby_update.c:12005` | `velocity.x = RandRange(-90, 90);` | -90->-107 (1), 90->108 (1) | 51 |
| `moby_update.c:12006` | `velocity.y = RandRange(-90, 90);` | -90->-107 (1), 90->108 (1) | 51 |
| `moby_update.c:12532` | `if (props->m_0x5c == 2 && props->m_0x68 >= 2) {` | 80->67 (2) | 63 |
| `moby_update.c:12623` | `if (distance > 0x4800 && nearestNode >= 0x11 &&` | 200->167 (3) | 63 |
| `moby_update.c:12691` | `switch (PATH_CUR_NODE(props->m_0x00).unk_0xC & 3) {` | 320->384 (1) | 63 |
| `moby_update.c:13007` | `if (MobyMoveWithGravity(moby, &props->m_0x30, props->m_0x2c,` | 12->15 (1) | 45 |
| `moby_update.c:13062` | `if (MobyMoveWithGravity(moby, &props->m_0x20, props->m_0x1c,` | 12->15 (1) | 55 |
| `moby_update.c:13212` | `if (func_80039E94(moby, props->m_0x00, 0x100, RT(150, 180), 0, RT(8, 1` | 256->307 (1) | 12 |
| `moby_update.c:13244` | `if (TICK_TIMER(props->m_0x08) && DISTANCE_TO_SPYRO(moby) < 1400 &&` | 180->150 (1) | 12 |
| `moby_update.c:13253` | `if (moby->m_AnimationState.m_Animation == 6) {` | 140->117 (1) | 12 |
| `moby_update.c:13286` | `MobyMoveWithGravity(moby, &props->m_0x10, props->m_0x0c, &props->m_0x1` | 12->15 (1) | 12 |
| `moby_update.c:13494` | `if (func_80039398(moby, 0x78, 0, 500, 0x15) != 0) {` | 120->100 (1) | 12 |
| `moby_update.c:13520` | `if (func_80017908(angle, angle2) >= 0x20) {` | 120->100 (1) | 12 |
| `moby_update.c:13547` | `if (D_80075904 < 0xf) {` | 15->13 (1) | 12 |
| `moby_update.c:14395` | `if (MobyMoveWithGravity(moby, &props->m_0x18, props->m_0x14,` | 12->15 (1) | 64 |
| `moby_update.c:14419` | `if ((moby->m_DamageFlags &` | 150->180 (1) | 64 |
| `moby_update.c:14689` | `MobyMoveWithGravity(moby, &props->m_0x18, props->m_0x14, &props->m_0x1` | 16->19 (1) | 64 |
| `moby_update.c:15136` | `} else {` | 27->22 (1) | 21 |
| `moby_update.c:15154` | `if (props->m_0x14 == 0) {` | 120->100 (1) | 21 |
| `moby_update.c:15405` | `if (DISTANCE_TO_SPYRO(moby) < 0x800 \|\|` | 256->307 (1) | 21 |
| `moby_update.c:15905` | `if (DISTANCE_TO_SPYRO(moby) < 0x1C00 &&` | 72->87 (2) | 23 |
| `moby_update.c:16088` | `MobyMoveWithGravity(moby, &props->m_0x0c, props->m_0x08, &props->m_0x1` | 12->15 (1) | 61 |
| `moby_update.c:16117` | `switch (moby->m_State) {` | 2->3 (1) | 61 |
| `moby_update.c:16412` | `switch (moby->m_State) {` | 2->3 (1) | 61 |
| `moby_update.c:16537` | `} else if ((g_SurfaceBelowFlags & 0x3F) == 0) {` | -10->-12 (1) | 61 |
| `moby_update.c:16759` | `if (TICK_TIMER(props->m_0x14) && distance < attackDistance &&` | 31->26 (1) | 53 |
| `moby_update.c:16950` | `if (func_80039E94(moby, props->m_0x00, 0x100, RT(0xA0, 0xC0), 0, RT(8,` | 256->307 (1) | 53 |
| `moby_update.c:17310` | `} else if ((g_SurfaceBelowFlags & 0x3F) == 0) {` | -10->-12 (1) | 61 |
| `moby_update.c:18025` | `distance = func_80038638(moby, &linkedMoby->m_Position, 1500, result, ` | 14->17 (1) | 20 |
| `moby_update.c:18274` | `} else {` | 37->31 (1) | 20 |
| `moby_update.c:18275` | `if (moby->m_AnimationState.m_NextFrame >= RT(37, 31) &&` | 50->42 (1) | 20 |
| `moby_update.c:18308` | `if (moby->m_AnimationState.m_NextFrame == 0xB) {` | 11->9 (1) | 20 |
| `moby_update.c:18864` | `result = MobyMoveWithGravity(moby, &props->m_0x08, props->m_0x14,` | 12->15 (1) | 22 |
| `moby_update.c:18923` | `if (!moby->m_WasDrawn) {` | 100->120 (1) | 22 |
| `moby_update.c:18929` | `result = func_80039E94(moby, props->m_0x00, RT(0x80, 0x9A), speed, 0, ` | 4->5 (1) | 22 |
| `moby_update.c:18940` | `if (DISTANCE_TO_SPYRO(moby) >= 1500 &&` | 4->5 (1) | 22 |
| `moby_update.c:19078` | `if (moby->m_AnimationState.m_NextAnimation == 2 &&` | 15->13 (1) | 22 |
| `moby_update.c:19559` | `if ((moby->m_DamageFlags & (MOBY_DAMAGE_CHARGE \| MOBY_DAMAGE_SUPER)) ` | 128->154 (1) | 23 |
| `moby_update.c:19609` | `if (DISTANCE_TO_SPYRO(moby) < 0x1C00 &&` | 72->87 (2) | 23 |
| `moby_update.c:19692` | `MobyMoveWithGravity(moby, &props->m_Scratch,` | 8->10 (1) | 23 |
| `moby_update.c:19781` | `if (MobyMoveWithGravity(moby, &props->m_0x08, props->m_0x00,` | 12->15 (1) | 55 |
| `moby_update.c:20318` | `if (TICK_TIMER(props->m_0x30) &&` | 256->307 (1) | 31 |
| `moby_update.c:20869` | `if ((*(u_int *)&moby->m_AnimationState & 0xFF00FF00) ==` | 5120->4352 (2) | 30, 31 |
| `moby_update.c:21146` | `break;` | 20->17 (2) | 30, 31 |
| `moby_update.c:21218` | `if (linkedMoby->m_State < 0x80 &&` | 90->75 (2) | 30, 31 |
| `moby_update.c:21248` | `func_80039E94(moby, props->m_0x00, 0x100, RT(0x78, 0x90), 200, 6, 0x28` | 256->307 (2), 6->7 (2) | 30, 31 |
| `moby_update.c:21334` | `if (func_80039E94(moby, props->m_0x00, 0x100, RT(0x8C, 0xA8), 0, 0x10,` | 256->307 (2) | 30, 31 |
| `moby_update.c:21492` | `if (props->m_0x14 == 2) {` | 160->133 (2) | 30, 31 |
| `moby_update.c:22337` | `MobyMoveWithGravity(moby, &props->m_0x1c, props->m_0x20, &props->m_0x2` | 12->15 (1) | 33 |
| `moby_update.c:22547` | `if ((moby->m_DamageFlags &` | 60->72 (1) | 43 |
| `moby_update.c:22738` | `switch (PATH_CUR_NODE(props->m_0x04).unk_0xC & 3) {` | 320->384 (1) | 43 |
| `moby_update.c:22774` | `MobyMoveWithGravity(moby, &props->m_0x0c, props->m_0x10, &props->m_0x1` | 12->15 (1) | 43 |
| `moby_update.c:22871` | `switch (PATH_CUR_NODE(props->m_0x04).unk_0xC & 3) {` | 200->240 (1) | 43 |
| `moby_update.c:23708` | `if (MoveMobyAlongPath(moby, props->m_0x00, 0x100, RT(0x40, 0x4D), 0, R` | 256->307 (1) | 32 |
| `moby_update.c:24138` | `if (MobyMoveWithGravity(moby, &props->m_0x1c, props->m_0x18,` | 12->15 (3) | 15, 35, 45 |
| `moby_update.c:24155` | `if (MobyMoveWithGravity(moby, &props->m_0x1c, props->m_0x18,` | 12->15 (3) | 15, 35, 45 |
| `moby_update.c:24249` | `switch (moby->m_Substate) {` | 2->3 (1) | 61 |
| `moby_update.c:24291` | `if ((moby->m_DamageFlags & MOBY_DAMAGE_FROM_MOBY) &&` | 90->75 (13) | 21, 31, 32, 34, 40, 44, 50, 51 … |
| `moby_update.c:25381` | `if (moby->m_AnimationState.m_NextFrame < 0x0D) {` | 13->11 (1), 14->11 (1) | 14 |
| `moby_update.c:26738` | `result = MobyMoveWithGravity(moby, &props->m_0x10, props->m_0x0c,` | 12->15 (1) | 24 |
| `moby_update.c:26886` | `MobyMoveWithGravity(moby, &props->m_0x10, props->m_0x0c, &props->m_0x0` | 12->15 (1) | 24 |
| `moby_update.c:26908` | `result = func_80039E94(moby, props->m_0x34, 0x100, props->m_0x1c, 0, R` | 256->307 (1) | 24 |
| `moby_update.c:26947` | `if (func_80017908(moby->m_Rotation.z, angle) >= ROTDEG8(120) \|\|` | 29->21 (1) | 24 |
| `moby_update.c:27157` | `if (func_80017908(moby->m_Rotation.z, angle) < 8 &&` | 6->5 (1) | 24 |
| `moby_update.c:28459` | `MobyMoveWithGravity(moby, &props->m_0x0c, props->m_0x08, &props->m_0x1` | 12->15 (1) | 42 |
| `moby_update.c:28664` | `if ((moby->m_DamageFlags & MOBY_DAMAGE_SUPER) && moby->m_State != 1) {` | 15->13 (2) | 15, 55 |
| `moby_update.c:29192` | `if (func_80039E94(moby, props->m_0x0c, 0x100, RT(0x78, 0x90), 0, RT(8,` | 256->307 (1) | 43 |
| `moby_update.c:29344` | `if (TICK_TIMER(props->m_0x1c) \|\|` | 12->15 (2) | 40, 41 |
| `moby_update.c:29482` | `MobyMoveWithGravity(moby, &props->m_0x14, props->m_0x10, &props->m_0x1` | 16->19 (1) | 40 |
| `moby_update.c:29522` | `if (func_80039E94(moby, props->m_0x0c, 0x100, RT(0x78, 0x90), 0, RT(8,` | 256->307 (4) | 40, 41, 42, 44 |
| `moby_update.c:29598` | `if (func_80039E94(moby, props->m_0x04, 0x100, RT(110, 132), 0, RT(8, 1` | 256->307 (1) | 41 |
| `moby_update.c:29656` | `if (props->m_0x38 == 0 && moby->m_AnimationState.m_NextFrame > 1 &&` | 26->21 (1) | 41 |
| `moby_update.c:29794` | `if (moby->m_AnimationState.m_NextFrame > RT(7, 6) &&` | 8->6 (1) | 41 |
| `particle_spawn.c:355` | `if (g_Spyro.m_Physics.m_SpeedAngle.m_Speed > RT(0x5780, 0x6900)) {` | 16961->20353 (35) | 10, 11, 12, 13, 14, 15, 20, 21 … |
