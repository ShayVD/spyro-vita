# R3 PAL retune map: the camera and func_8003FE40

Source: `tools/regions/retune.py context FUNCTION` (D135). Every one of these functions differs from PAL only in constants (no unpaired ranges). Edits are `RT(us, pal)` with the US literal as it was.

## Camera (`src/game/camera_hand.c`; func_80035F58 is the decomp's `src/camera.c`)

| Function | US addr | US -> PAL | Hand C after the edit | How |
|---|---|---|---|---|
| func_80034CE8 | 80034d0c, 80034d18 | slti/li 1024 -> 1475 (x1.44) | `if (... && pull < RT(0x400, 1475)) pull = RT(0x400, 1475);` | the azimuth spring's minimum pull in state 3 (a stiffness: an acceleration) |
| func_80034CE8 | 80034dd0 | slti 1024 -> 1229 (x1.2) | `m_Speed < RT(0x400, 1229)` | the passive camera's "slow Spyro" test (a speed) |
| func_80034CE8 | 80034f18, 80034f20, 80034f30, 80034f38 | slti -2048, li -2048, slti 2049, li 2048 -> -2458, -2458, 2458, 2457 | `ClampSpeedRange(SPH_AZIMUTH, RT(-0x800, -2458), RT(0x800, 2457))` | the first of the two ±0x800 azimuth clamps (state 15, walking state 11). The asm's second clamp (80035050..80035078) is unchanged in PAL, so the shared `ClampSpeed(…, 0x800)` call stays; PAL's limits are asymmetric, hence `ClampSpeedRange` |
| func_800357A4 | 80035824, 80035d30 | li 45 -> 38 (x5/6) | `StartRecentre`: `D_80075938 = RT(45, 38)` | the recentre timer; both asm copies are the one inlined helper |
| func_800357A4 | 80035bb4 | slti 1025 -> 1230 (x1.2) | `m_Speed > RT(0x400, 1229)` | `> 0x400` is `slti 0x401` |
| func_800357A4 | 80035e6c, 80035e74 | li 8 -> 7, li 90 -> 75 (x5/6) | `D_80075938 = RT(8, 7); … D_80075894 = RT(90, 75);` | the passive camera's slow recentre |
| func_80035F58 | 80035f98, 80035fa4 | li -1024, 1024 -> -1229, 1229 (x1.2) | decomp `src/camera.c`: `D_80075924 = -RT(1024, 1229)`, `RT(1024, 1229)` | L2/R2 turn speed; patch `0004-pal-timing-main` |
| func_80035FB4 | 8003624c, 80036260, 80036270, 80036378 | li 90 -> 75 (x5/6) | the follow state's `D_80075894 = RT(90, 75)` | one hand-C literal for the four inlined copies; every asm 90 in the function is a site |
| func_80035FB4 | 8003737c | slti 1024 -> 1229 (x1.2) | end of `func_80035FB4`: `m_Speed < RT(0x400, 1229)` | |
| (port) | — | — | `FreeCameraTurn`: `RT(-x * 8 * speed / 100, -x * 1229 / 128 * speed / 100)` | the free camera (D47) matches L2/R2's full turn, which PAL scales |

## func_8003FE40 (`src/game/pete_state_hand.c`)

| US addr | US -> PAL | Hand C | How |
|---|---|---|---|
| 800402c4 | slti 30 -> 25 (x5/6) | `if (D_80075948 < RT(30, 25))` | the "camera behind the moby for 30 ticks" wait |
| 8004089c, 800408a4 | slti 12, li 12 -> 10, 10 (x5/6) | `Rumble(&D_800757D0, RT(0xC, 10))` | `Rumble` is `if (*t < min) *t = min`; the other 0xC instances in the function (7 of 9) are not sites and are other code |
