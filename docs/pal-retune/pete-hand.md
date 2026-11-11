# func_80043FE4 (src/game/pete_hand.c): PAL R3 retune map

Source: `tools/regions/retune.py context func_80043FE4` (178 constant sites, 60 unpaired ranges). Line numbers are in the edited `src/game/pete_hand.c`.

## Constant sites

| site | US addr | US -> PAL | hand-C line after edit | how derived |
|---|---|---|---|---|
| 0 | 800440f0 | slti 45 -> 38 (x5/6) | 424: `} else if (g_Spyro.m_idleTimer >= RT(45, 38) && !g_SpyroFlame.m_IsFlameActive) {` | UpdateIdle: `m_idleTimer >= 45` = slti 45; PAL slti 38 -> >= 38 |
| 1 | 800441fc | slti 1024 -> 1229 (x1.2) | 352: `if (speed < RT(0x400, 1229))` | TurnWhileMoving (inlined in UpdateRunning): `speed < 0x400` |
| 2 | 80044204 | slti 3200 -> 3840 (x1.2) | 355: `UpdateSpyroTurnMomentum(speed < RT(0xC80, 3840) ? 1 : 2);` | TurnWhileMoving (UpdateRunning): `speed < 0xC80` |
| 3 | 80044250 | addiu -192 -> -276 (x1.44) | 450: `SlowDown(RT(0xC0, 276));` | UpdateRunning: SlowDown(0xC0) -> addiu -0xC0 on m_Speed (+0x118) |
| 4 | 80044278 | addiu 320 -> 461 (x1.44) | 361: `func_8003D92C(pTurnLeft <= 0x200 ? RT(0x140, 461) : 0, RT(0xC0, 276));` | AccelerateUnlessTurning (UpdateRunning): func_8003D92C a0 = 0x140 |
| 5 | 80044284 | addiu 192 -> 276 (x1.44) | 361: `func_8003D92C(pTurnLeft <= 0x200 ? RT(0x140, 461) : 0, RT(0xC0, 276));` | AccelerateUnlessTurning (UpdateRunning): func_8003D92C a1 = 0xC0 |
| 6 | 800443f4 | addiu -256 -> -369 (x1.44) | 509: `SlowDown(RT(0x100, 369));` | UpdateTurnOnSpot: SlowDown(0x100) |
| 7 | 800444e8 | slti 1024 -> 1229 (x1.2) | 352: `if (speed < RT(0x400, 1229))` | TurnWhileMoving (inlined in UpdateState3); same literal as S1, both instances change |
| 8 | 800444f0 | slti 3200 -> 3840 (x1.2) | 355: `UpdateSpyroTurnMomentum(speed < RT(0xC80, 3840) ? 1 : 2);` | TurnWhileMoving (UpdateState3); same literal as S2 |
| 9 | 80044550 | addiu -192 -> -276 (x1.44) | 482: `SlowDown(RT(0xC0, 276));` | UpdateState3: SlowDown(0xC0) |
| 10 | 80044578 | addiu 320 -> 461 (x1.44) | 361: `func_8003D92C(pTurnLeft <= 0x200 ? RT(0x140, 461) : 0, RT(0xC0, 276));` | AccelerateUnlessTurning (UpdateState3) a0; same literal as S4 |
| 11 | 80044584 | addiu 192 -> 276 (x1.44) | 361: `func_8003D92C(pTurnLeft <= 0x200 ? RT(0x140, 461) : 0, RT(0xC0, 276));` | AccelerateUnlessTurning (UpdateState3) a1; same literal as S5 |
| 12 | 80044674 | addiu 3200 -> 3840 (x1.2) | 523: `func_8003D3B8(RT(0xC80, 3840));` | UpdateJump: func_8003D3B8(0xC80) |
| 13 | 80044680 | addiu 320 -> 461 (x1.44) | 525: `func_8003D92C(RT(0x140, 461), RT(0xC0, 276));` | UpdateJump: func_8003D92C(0x140, ...) |
| 14 | 80044688 | addiu 192 -> 276 (x1.44) | 525: `func_8003D92C(RT(0x140, 461), RT(0xC0, 276));` | UpdateJump: func_8003D92C(..., 0xC0) |
| 15 | 80044708 | slti 641 -> 923 (x1.44) | 532: `if (length > RT(0x280, 922))` | UpdateJump: `length > 0x280` = slti 0x281; PAL slti 923 -> > 922 |
| 16 | 80044720 | addiu 640 -> 922 (x1.44) | 533: `VecScaleToLength(&pull, length, RT(0x280, 922));` | UpdateJump: VecScaleToLength a2 0x280 |
| 17 | 80044744 | slti 13 -> 11 (x5/6) | 538: `if (pTicksInState < RT(13, 11) && g_Spyro.m_walkingState == 0 &&` | UpdateJump: `pTicksInState < 13`; gcc emitted the slti twice (both arms of the wall test), one literal |
| 18 | 800447b8 | slti 13 -> 11 (x5/6) | 538: `if (pTicksInState < RT(13, 11) && g_Spyro.m_walkingState == 0 &&` | UpdateJump: `pTicksInState < 13`; gcc emitted the slti twice (both arms of the wall test), one literal |
| 19 | 80044828 | addiu 215 -> 310 (x1.44) | 543: `ph->unk_0xe8.z += RT(0xD7, 310) + ph->m_gravity;` | UpdateJump: unk_0xe8.z += 0xD7 + gravity |
| 20 | 80044910 | slti 33 -> 39 (x1.2) | 566: `if (turn > RT(0x20, 38))` | UpdateAirborne: `turn > 0x20` = slti 0x21 (duplicated around the AngleDelta wrap); PAL slti 39 -> > 38 |
| 21 | 80044918 | slti 33 -> 39 (x1.2) | 566: `if (turn > RT(0x20, 38))` | UpdateAirborne: `turn > 0x20` = slti 0x21 (duplicated around the AngleDelta wrap); PAL slti 39 -> > 38 |
| 22 | 80044920 | slti -32 -> -38 (x1.2) | 568: `if (turn < RT(-0x20, -38))` | UpdateAirborne: `turn < -0x20` (slti -0x20, emitted twice) |
| 23 | 80044924 | addiu 32 -> 38 (x1.2) | 567: `turn = RT(0x20, 38);` | UpdateAirborne: turn = 0x20 |
| 24 | 80044928 | slti -32 -> -38 (x1.2) | 568: `if (turn < RT(-0x20, -38))` | UpdateAirborne: `turn < -0x20` (slti -0x20, emitted twice) |
| 25 | 80044934 | addiu -32 -> -38 (x1.2) | 569: `turn = RT(-0x20, -38);` | UpdateAirborne: turn = -0x20 |
| 26 | 80044950 | slti 128 -> 154 (x1.2) | 573: `if (ph->m_TrueSpeed < RT(0x80, 154)) {` | UpdateAirborne: m_TrueSpeed (+0x128) < 0x80 |
| 27 | 80044a30 | slti -8960 -> -10752 (x1.2) | 591: `if (ph->m_Acceleration.z < RT(-0x2300, -10752))` | UpdateAirborne: Acceleration.z (+0xFC) < -0x2300 |
| 28 | 80044a38 | addiu -8960 -> -10752 (x1.2) | 592: `ph->m_Acceleration.z = RT(-0x2300, -10752);` | UpdateAirborne: Acceleration.z = -0x2300 |
| 29 | 80044afc | addiu 320 -> 461 (x1.44) | 605: `func_8003D92C(RT(0x140, 461), RT(0xC0, 276));` | UpdateSidestep: func_8003D92C(0x140, ...) |
| 30 | 80044b00 | addiu 4096 -> 4915 (x1.2) | 604: `ph->m_TargetSpeedAngle.m_Speed = RT(0x1000, 4915);` | UpdateSidestep: m_TargetSpeedAngle.m_Speed (+0xC8) = 0x1000 (func_8003D3B8(0x1000) at 80044af0 is not a site, left) |
| 31 | 80044b10 | addiu 192 -> 276 (x1.44) | 605: `func_8003D92C(RT(0x140, 461), RT(0xC0, 276));` | UpdateSidestep: func_8003D92C(..., 0xC0) |
| 32 | 80044eec | slti 5120 -> 6144 (x1.2) | 644: `if (ph->m_SpeedAngle.m_Speed < RT(0x1400, 6144)) {` | UpdateSuperchargeSpeed (charge): m_Speed < 0x1400 |
| 33 | 80044ef4 | addiu 5120 -> 6144 (x1.2) | 645: `ph->m_TargetSpeedAngle.m_Speed = RT(0x1400, 6144);` | UpdateSuperchargeSpeed: target = 0x1400 (slti and li share the literal) |
| 34 | 80044f44 | addiu -192 -> -276 (x1.44) | 652: `ph->m_Acceleration.z -= RT(0xC0, 276);` | UpdateSuperchargeSpeed: onEdge Acceleration.z -= 0xC0 (asm jumps to the shared store 800477E4) |
| 35 | 80044fe0 | addiu 576 -> 829 (x1.44) | 729: `ph->m_SpeedAngle.m_Speed = RT(0x240, 829);` | UpdateCharge (no supercharge): m_Speed = 0x240 |
| 36 | 8004504c | addiu 384 -> 737 (other) | 740: `func_8003D92C(0, RT(0x180, 737));` | UpdateCharge released: func_8003D92C(0, 0x180); PAL 737 (x1.92, "other" in ctx) |
| 37 | 80045078 | slti 8961 -> 10753 (x1.2) | 744: `if (speed > RT(0x2300, 10752)) {` | UpdateCharge released: `speed > 0x2300` = slti 0x2301 -> > 10752 |
| 38 | 80045090 | addiu 8960 -> 10752 (x1.2) | 745: `VecScaleToLength(&ph->m_Acceleration, speed, RT(0x2300, 10752));` | UpdateCharge: VecScaleToLength a2 |
| 39 | 80045094 | addiu 8960 -> 10752 (x1.2) | 746: `ph->m_SpeedAngle.m_Speed = RT(0x2300, 10752);` | UpdateCharge: m_Speed = 0x2300 |
| 40 | 800450c0 | addiu -192 -> -276 (x1.44) | 749: `ph->m_Acceleration.z -= RT(0xC0, 276);` | UpdateCharge released: onEdge Acceleration.z -= 0xC0 |
| 41 | 800450f4 | slti 19 -> 16 (x5/6) | 782: `if (pTicksInState >= RT(19, 16))` | UpdateKnockback: `pTicksInState >= 19` = slti 0x13 |
| 42 | 8004514c | addiu 192 -> 276 (x1.44) | 768: `func_8003D92C(0, RT(0xC0, 276));` | UpdateHurt: func_8003D92C(0, 0xC0) |
| 43 | 80045164 | addiu -192 -> -276 (x1.44) | 390: `g_Spyro.m_Physics.m_Acceleration.z = g_Spyro.m_Physics.m_TrueVelocity.z - RT(0xC0, 276);` | ApplyFall, inlined in UpdateHurt (TrueVelocity +0x114 - 0xC0); every ApplyFall instance is a site (S43, S174) so the helper changes |
| 44 | 80045174 | slti -8960 -> -10752 (x1.2) | 759: `if (g_Spyro.m_Physics.m_Acceleration.z < RT(-0x2300, -10752))` | ClampFall: slti -0x2300 (delay-slot copies before `j .L80047450` in Hurt/Tumble25/Fall, and the shared tail in Glide); every ClampFall instance is a site |
| 45 | 8004519c | addiu -1536 -> -2212 (x1.44) | 793: `ph->m_Acceleration.z -= RT(0x600, 2212);` | UpdateTumble (state 25 path): Acceleration.z -= 0x600 |
| 46 | 800451ac | slti -8960 -> -10752 (x1.2) | 759: `if (g_Spyro.m_Physics.m_Acceleration.z < RT(-0x2300, -10752))` | ClampFall: slti -0x2300 (delay-slot copies before `j .L80047450` in Hurt/Tumble25/Fall, and the shared tail in Glide); every ClampFall instance is a site |
| 47 | 800451e4 | addiu -1536 -> -2212 (x1.44) | 793: `ph->m_Acceleration.z -= RT(0x600, 2212);` | UpdateTumble (state 27 path, compiled separately); same literal |
| 48 | 800451f0 | slti -8960 -> -10752 (x1.2) | 759: `if (g_Spyro.m_Physics.m_Acceleration.z < RT(-0x2300, -10752))` | ClampFall inlined in UpdateTumble(1) |
| 49 | 800451f8 | addiu -8960 -> -10752 (x1.2) | 760: `g_Spyro.m_Physics.m_Acceleration.z = RT(-0x2300, -10752);` | ClampFall: = -0x2300 |
| 50 | 80045224 | addiu 85 -> 102 (x1.2) | 799: `ph->m_SpeedAngle.m_RotZ += RT(0x55, 102);` | UpdateTumble spin: m_RotZ (+0x11C) += 0x55 |
| 51 | 80045464 | slti 4160 -> 4992 (x1.2) | 1294: `if (ph->m_SpeedAngle.m_Speed < RT(0x1040, 4992))` | FlyFree: m_Speed < 0x1040 |
| 52 | 8004546c | addiu 4160 -> 4992 (x1.2) | 1295: `pitchIn += (RT(0x1040, 4992) - ph->m_SpeedAngle.m_Speed) >> 6;` | FlyFree: 0x1040 - m_Speed |
| 53 | 80045570 | slti 8 -> 10 (x1.2) | 1312: `if (lift >= RT(8, 10))` | FlyFree: `lift >= 8` = slti 8 (branch to clamp) |
| 54 | 8004557c | addiu 7 -> 9 (x1.2) | 1313: `lift = RT(7, 9);` | FlyFree: lift = 7 |
| 55 | 80045668 | addiu 1054 -> 1308 (other) | 1326: `ph->m_SpeedAngle.m_Speed += RT(0x41E, 1308);` | FlyFree: !g_IsFlightLevel m_Speed += 0x41E ("other", x1.241) |
| 56 | 80045690 | addiu 306 -> 441 (x1.44) | 1329: `ceiling = (below + RT(0x132, 441)) >> 2;` | FlyFree: ceiling = (below + 0x132) >> 2 |
| 57 | 800456ec | slti 1920 -> 2304 (x1.2) | 1336: `if (ph->m_SpeedAngle.m_Speed < RT(0x780, 2304))` | FlyFree: m_Speed < 0x780 (FlyFree's own floor, not EaseSpeedTo) |
| 58 | 800456f4 | addiu 1920 -> 2304 (x1.2) | 1337: `ph->m_SpeedAngle.m_Speed = RT(0x780, 2304);` | FlyFree: m_Speed = 0x780 |
| 59 | 80045900 | addiu 30 -> 36 (x1.2) | 1358: `ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + RT(0x1E, 36));` | FlyLoop: RotY + 0x1E |
| 60 | 8004599c | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 61 | 800459a4 | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 62 | 800459ac | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 63 | 800459b0 | addiu 16 -> 19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value 0x10 |
| 64 | 800459b4 | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 65 | 800459c0 | addiu -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value -0x10 |
| 66 | 80045a4c | slti 7680 -> 9216 (x1.2) | 1370: `if (ph->m_SpeedAngle.m_Speed < RT(0x1E00, 9216) && !g_Spyro.unk_0x254)` | FlyLoop: m_Speed < 0x1E00 |
| 67 | 80045af4 | slti 31 -> 37 (x1.2) | 1386: `ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(pitchStep, RT(-0x1E, -36), RT(0x1E, 36)));` | FlyTurnAround: Clamp(pitchStep, -0x1E, 0x1E) max (slti 0x1F -> > 36) |
| 68 | 80045afc | slti -30 -> -36 (x1.2) | 1386: `ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(pitchStep, RT(-0x1E, -36), RT(0x1E, 36)));` | FlyTurnAround: clamp min (slti -0x1E, twice) |
| 69 | 80045b00 | addiu 30 -> 36 (x1.2) | 1386: `ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(pitchStep, RT(-0x1E, -36), RT(0x1E, 36)));` | FlyTurnAround: clamp value 0x1E |
| 70 | 80045b04 | slti -30 -> -36 (x1.2) | 1386: `ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(pitchStep, RT(-0x1E, -36), RT(0x1E, 36)));` | FlyTurnAround: clamp min (slti -0x1E, twice) |
| 71 | 80045b10 | addiu -30 -> -36 (x1.2) | 1386: `ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(pitchStep, RT(-0x1E, -36), RT(0x1E, 36)));` | FlyTurnAround: clamp value -0x1E |
| 72 | 80045be0 | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 73 | 80045be8 | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 74 | 80045bf0 | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 75 | 80045bf4 | addiu 16 -> 19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value 0x10 |
| 76 | 80045bf8 | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 77 | 80045c04 | addiu -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value -0x10 |
| 78 | 80045c20 | slti 65 -> 77 (x1.2) | 1395: `D_800758A0 = Clamp(D_800758A0 + RollToLevel(), RT(-0x40, -76), RT(0x40, 76));` | FlyTurnAround: D_800758A0 = Clamp(.., -0x40, 0x40) max (slti 0x41 -> > 76; li 0x40) |
| 79 | 80045c28 | addiu 64 -> 76 (x1.2) | 1395: `D_800758A0 = Clamp(D_800758A0 + RollToLevel(), RT(-0x40, -76), RT(0x40, 76));` | FlyTurnAround: D_800758A0 = Clamp(.., -0x40, 0x40) max (slti 0x41 -> > 76; li 0x40) |
| 80 | 80045c40 | slti -64 -> -76 (x1.2) | 1395: `D_800758A0 = Clamp(D_800758A0 + RollToLevel(), RT(-0x40, -76), RT(0x40, 76));` | FlyTurnAround: clamp min -0x40 (slti, li) |
| 81 | 80045c48 | addiu -64 -> -76 (x1.2) | 1395: `D_800758A0 = Clamp(D_800758A0 + RollToLevel(), RT(-0x40, -76), RT(0x40, 76));` | FlyTurnAround: clamp min -0x40 (slti, li) |
| 82 | 80045ca0 | addiu 30 -> 36 (x1.2) | 1402: `ph->m_SpeedAngle.m_RotY = AngleDelta(ph->m_SpeedAngle.m_RotY + RT(0x1E, 36));` | FlyTurnAround (not flipped yet): RotY + 0x1E |
| 83 | 80045d30 | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 84 | 80045d38 | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 85 | 80045d40 | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 86 | 80045d44 | addiu 16 -> 19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value 0x10 |
| 87 | 80045d48 | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 88 | 80045d54 | addiu -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value -0x10 |
| 89 | 80045dec | slti 7680 -> 9216 (x1.2) | 1415: `if (ph->m_SpeedAngle.m_Speed < RT(0x1E00, 9216) && !g_Spyro.unk_0x254 && ph->m_SpeedAngle.m_RotY < 0x400)` | FlyTurnAround: m_Speed < 0x1E00 |
| 90 | 80045e9c | slti 31 -> 37 (x1.2) | 1432: `AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(AngleDelta(0x400 - ph->m_SpeedAngle.m_RotY), RT(-0x1E, -36), RT(0x1E, 36)));` | FlyClimbOut: Clamp(AngleDelta(0x400 - RotY), -0x1E, 0x1E) max (slti 0x1F, twice) |
| 91 | 80045ea4 | slti 31 -> 37 (x1.2) | 1432: `AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(AngleDelta(0x400 - ph->m_SpeedAngle.m_RotY), RT(-0x1E, -36), RT(0x1E, 36)));` | FlyClimbOut: Clamp(AngleDelta(0x400 - RotY), -0x1E, 0x1E) max (slti 0x1F, twice) |
| 92 | 80045eac | slti -30 -> -36 (x1.2) | 1432: `AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(AngleDelta(0x400 - ph->m_SpeedAngle.m_RotY), RT(-0x1E, -36), RT(0x1E, 36)));` | FlyClimbOut: clamp min (slti -0x1E, twice) |
| 93 | 80045eb0 | addiu 30 -> 36 (x1.2) | 1432: `AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(AngleDelta(0x400 - ph->m_SpeedAngle.m_RotY), RT(-0x1E, -36), RT(0x1E, 36)));` | FlyClimbOut: clamp value 0x1E |
| 94 | 80045eb4 | slti -30 -> -36 (x1.2) | 1432: `AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(AngleDelta(0x400 - ph->m_SpeedAngle.m_RotY), RT(-0x1E, -36), RT(0x1E, 36)));` | FlyClimbOut: clamp min (slti -0x1E, twice) |
| 95 | 80045ec0 | addiu -30 -> -36 (x1.2) | 1432: `AngleDelta(ph->m_SpeedAngle.m_RotY + Clamp(AngleDelta(0x400 - ph->m_SpeedAngle.m_RotY), RT(-0x1E, -36), RT(0x1E, 36)));` | FlyClimbOut: clamp value -0x1E |
| 96 | 80045f34 | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 97 | 80045f3c | slti 17 -> 20 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp max 0x10 (`> 0x10` = slti 0x11, duplicated around the wrap) in FlyLoop/FlyTurnAround(x2)/FlyClimbOut; all instances are sites so the helper changes |
| 98 | 80045f44 | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 99 | 80045f48 | addiu 16 -> 19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value 0x10 |
| 100 | 80045f4c | slti -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: Clamp min -0x10 (slti -0x10) |
| 101 | 80045f58 | addiu -16 -> -19 (x1.2) | 1054: `static int RollToLevel(void) { return Clamp(AngleDelta(-g_Spyro.m_Physics.m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)); }` | RollToLevel: clamp value -0x10 |
| 102 | 80045f64 | addiu 4160 -> 4992 (x1.2) | 1436: `ph->m_SpeedAngle.m_Speed += Clamp(RT(0x1040, 4992) - ph->m_SpeedAngle.m_Speed, -0x80, 0x80);` | FlyClimbOut: 0x1040 - m_Speed (the +-0x80 clamp is not a site) |
| 103 | 80045fcc | addiu 6400 -> 7680 (x1.2) | 1447: `ph->m_TargetSpeedAngle.m_Speed = RT(0x1900, 7680);` | FlyIn: target speed 0x1900 (stored to +0xC8) |
| 104 | 80045fd0 | slti 6400 -> 7680 (x1.2) | 1448: `EaseSpeedTo(RT(0x1900, 7680));` | FlyIn: EaseSpeedTo(0x1900) inlined; the two slti 0x1900 are pTarget folded |
| 105 | 80045fdc | addiu 128 -> 184 (x1.44) | 1041: `ph->m_SpeedAngle.m_Speed += RT(0x80, 184);` | EaseSpeedTo (inlined in FlyIn): += 0x80 |
| 106 | 80045ff4 | addiu -128 -> -184 (x1.44) | 1045: `ph->m_SpeedAngle.m_Speed -= RT(0x80, 184);` | EaseSpeedTo (FlyIn): -= 0x80 |
| 107 | 80045ffc | slti 6400 -> 7680 (x1.2) | 1448: `EaseSpeedTo(RT(0x1900, 7680));` | FlyIn: EaseSpeedTo(0x1900) inlined; the two slti 0x1900 are pTarget folded |
| 108 | 8004601c | slti 1920 -> 2304 (x1.2) | 1049: `if (ph->m_SpeedAngle.m_Speed < RT(0x780, 2304))` | EaseSpeedTo (FlyIn): floor test 0x780 |
| 109 | 80046024 | addiu 1920 -> 2304 (x1.2) | 1050: `ph->m_SpeedAngle.m_Speed = RT(0x780, 2304);` | EaseSpeedTo (FlyIn): floor 0x780 |
| 110 | 8004604c | addiu 30 -> 36 (x1.2) | 1457: `pitch = (ph->m_SpeedAngle.m_RotY + RT(0x1E, 36)) & 0xFFF;` | FlyIn: pitch step 0x1E (li s3 0x1E) |
| 111 | 80046140 | slti 17 -> 20 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (after FlyIn): clamp max (slti 0x11, twice) |
| 112 | 80046148 | slti 17 -> 20 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (after FlyIn): clamp max (slti 0x11, twice) |
| 113 | 80046150 | slti -16 -> -19 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (FlyIn): clamp min slti -0x10 (emitted once here) |
| 114 | 80046158 | addiu 16 -> 19 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (FlyIn): value 0x10 |
| 115 | 80046164 | addiu -16 -> -19 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (FlyIn): value -0x10 |
| 116 | 80046304 | addiu 9 -> 89 (other) | NOT CHANGED: line 1508 | SpawnWingTrails: wing-tip model vertex 9 (+0x5B -> 100); PAL uses 89 (+11 -> 100). A model-data/vertex-index difference, not a retune: left out of RT, comment added. |
| 117 | 800463a0 | addiu 91 -> 11 (other) | NOT CHANGED: line 1508 | SpawnWingTrails: wing-tip model vertex 9 (+0x5B -> 100); PAL uses 89 (+11 -> 100). A model-data/vertex-index difference, not a retune: left out of RT, comment added. |
| 118 | 80046504 | addiu 6400 -> 7680 (x1.2) | 1141: `: RT((int)((u_int)(ahead * 35) << 7) >> 7,` | UpdatePortalFlight: `formula + 0x1900` (same statement, next line holds `+ RT(0x1900, 7680)`) |
| 119 | 80046508 | addiu 6400 -> 7680 (x1.2) | 1140: `ph->m_TargetSpeedAngle.m_Speed = ahead >= 0 ? RT(0x1900, 7680)` | UpdatePortalFlight: ahead >= 0 ? 0x1900 |
| 120 | 80046530 | addiu 128 -> 184 (x1.44) | 1041: `ph->m_SpeedAngle.m_Speed += RT(0x80, 184);` | EaseSpeedTo (inlined in UpdatePortalFlight): += 0x80; every EaseSpeedTo instance is a site |
| 121 | 80046548 | addiu -128 -> -184 (x1.44) | 1045: `ph->m_SpeedAngle.m_Speed -= RT(0x80, 184);` | EaseSpeedTo (portal): -= 0x80 |
| 122 | 80046570 | slti 1920 -> 2304 (x1.2) | 1049: `if (ph->m_SpeedAngle.m_Speed < RT(0x780, 2304))` | EaseSpeedTo (portal): < 0x780 |
| 123 | 80046578 | addiu 1920 -> 2304 (x1.2) | 1050: `ph->m_SpeedAngle.m_Speed = RT(0x780, 2304);` | EaseSpeedTo (portal): = 0x780 |
| 124 | 80046754 | slti -1920 -> -2304 (x1.2) | 1084: `if (ph->m_Acceleration.z < RT(-0x780, -2304))` | PortalFall (case 11 / default): Acceleration.z < -0x780; both instances are sites |
| 125 | 8004675c | addiu -1920 -> -2304 (x1.2) | 1085: `ph->m_Acceleration.z = RT(-0x780, -2304);` | PortalFall: = -0x780 |
| 126 | 80046794 | slti 16 -> 13 (x5/6) | 1093: `if (g_Spyro.m_idleTimer < RT(16, 13))` | PortalPitchStep: m_idleTimer (+0x80) < 16 |
| 127 | 80046848 | slti -60 -> -72 (x1.2) | 1105: `return step < RT(-0x3C, -72) ? RT(-0x3C, -72) : step > RT(0x3C, 72) ? RT(0x3C, 72) : step;` | PortalPitchStep near floor: step < -0x3C ? -0x3C |
| 128 | 80046850 | slti 61 -> 73 (x1.2) | 1105: `return step < RT(-0x3C, -72) ? RT(-0x3C, -72) : step > RT(0x3C, 72) ? RT(0x3C, 72) : step;` | PortalPitchStep near floor: step > 0x3C (slti 0x3D, twice) ? 0x3C |
| 129 | 80046854 | addiu -60 -> -72 (x1.2) | 1105: `return step < RT(-0x3C, -72) ? RT(-0x3C, -72) : step > RT(0x3C, 72) ? RT(0x3C, 72) : step;` | PortalPitchStep near floor: step < -0x3C ? -0x3C |
| 130 | 80046858 | slti 61 -> 73 (x1.2) | 1105: `return step < RT(-0x3C, -72) ? RT(-0x3C, -72) : step > RT(0x3C, 72) ? RT(0x3C, 72) : step;` | PortalPitchStep near floor: step > 0x3C (slti 0x3D, twice) ? 0x3C |
| 131 | 80046868 | addiu 60 -> 72 (x1.2) | 1105: `return step < RT(-0x3C, -72) ? RT(-0x3C, -72) : step > RT(0x3C, 72) ? RT(0x3C, 72) : step;` | PortalPitchStep near floor: step > 0x3C (slti 0x3D, twice) ? 0x3C |
| 132 | 80046884 | addiu 30 -> 36 (x1.2) | 1111: `return RT(0x1E, 36);` | PortalPitchStep: return 0x1E (li s3 in the bnez delay slot) |
| 133 | 800468fc | slti -30 -> -36 (x1.2) | 1118: `return step < RT(-0x1E, -36) ? RT(-0x1E, -36) : step > RT(0x1E, 36) ? RT(0x1E, 36) : step;` | PortalPitchStep: step < -0x1E ? -0x1E |
| 134 | 80046904 | slti 31 -> 37 (x1.2) | 1118: `return step < RT(-0x1E, -36) ? RT(-0x1E, -36) : step > RT(0x1E, 36) ? RT(0x1E, 36) : step;` | PortalPitchStep: step > 0x1E (slti 0x1F, twice) ? 0x1E |
| 135 | 80046908 | addiu -30 -> -36 (x1.2) | 1118: `return step < RT(-0x1E, -36) ? RT(-0x1E, -36) : step > RT(0x1E, 36) ? RT(0x1E, 36) : step;` | PortalPitchStep: step < -0x1E ? -0x1E |
| 136 | 8004690c | slti 31 -> 37 (x1.2) | 1118: `return step < RT(-0x1E, -36) ? RT(-0x1E, -36) : step > RT(0x1E, 36) ? RT(0x1E, 36) : step;` | PortalPitchStep: step > 0x1E (slti 0x1F, twice) ? 0x1E |
| 137 | 8004691c | addiu 30 -> 36 (x1.2) | 1118: `return step < RT(-0x1E, -36) ? RT(-0x1E, -36) : step > RT(0x1E, 36) ? RT(0x1E, 36) : step;` | PortalPitchStep: step > 0x1E (slti 0x1F, twice) ? 0x1E |
| 138 | 800469b8 | slti -1920 -> -2304 (x1.2) | 1084: `if (ph->m_Acceleration.z < RT(-0x780, -2304))` | PortalFall (case 11 / default): Acceleration.z < -0x780; both instances are sites |
| 139 | 800469c0 | addiu -1920 -> -2304 (x1.2) | 1085: `ph->m_Acceleration.z = RT(-0x780, -2304);` | PortalFall: = -0x780 |
| 140 | 800469fc | slti 17 -> 20 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (end of UpdatePortalFlight): clamp max; all BankIntoTurn instances are sites |
| 141 | 80046a04 | slti 17 -> 20 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (end of UpdatePortalFlight): clamp max; all BankIntoTurn instances are sites |
| 142 | 80046a0c | slti -16 -> -19 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (portal): clamp min |
| 143 | 80046a14 | addiu 16 -> 19 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (portal): value 0x10 |
| 144 | 80046a20 | addiu -16 -> -19 (x1.2) | 1064: `AddRoll(Clamp(AngleDelta(-ph->m_TurnMomentum * 8 - ph->m_SpeedAngle.m_RotX), RT(-0x10, -19), RT(0x10, 19)));` | BankIntoTurn (portal): value -0x10 |
| 145 | 80046a88 | slti -8960 -> -10752 (x1.2) | 759: `if (g_Spyro.m_Physics.m_Acceleration.z < RT(-0x2300, -10752))` | ClampFall: slti -0x2300 (delay-slot copies before `j .L80047450` in Hurt/Tumble25/Fall, and the shared tail in Glide); every ClampFall instance is a site |
| 146 | 80046b08 | addiu 128 -> 184 (x1.44) | 836: `rise = ph->unk_0xe8.z + RT(0x80, 184);` | UpdateWhirlwind bottom: unk_0xe8.z (+0xF0) + 0x80 |
| 147 | 80046b0c | slti 4801 -> 5761 (x1.2) | 837: `if (rise > RT(0x12C0, 5760))` | UpdateWhirlwind: `rise > 0x12C0` = slti 0x12C1 -> > 5760 |
| 148 | 80046b18 | addiu 4800 -> 5760 (x1.2) | 838: `rise = RT(0x12C0, 5760);` | UpdateWhirlwind bottom: rise = 0x12C0 |
| 149 | 80046b44 | addiu 1024 -> 1229 (x1.2) | 842: `ph->unk_0x144 = RT(0x400, 1229);` | UpdateWhirlwind bottom: unk_0x144 = 0x400 |
| 150 | 80046bf0 | addiu 3200 -> 3840 (x1.2) | 851: `ph->unk_0x144 = RT(0xC80, 3840);` | UpdateWhirlwind middle: unk_0x144 = 0xC80 |
| 151 | 80046c18 | addiu 4800 -> 5760 (x1.2) | 838: `rise = RT(0x12C0, 5760);` | UpdateWhirlwind middle: rise = 0x12C0 |
| 152 | 80046d40 | addiu 3200 -> 3840 (x1.2) | 873: `ph->unk_0x144 = RT(0xC80, 3840);` | UpdateWhirlwind top: unk_0x144 = 0xC80 |
| 153 | 80046e08 | addiu 8 -> 10 (x1.2) | 898: `TurnBodyToVelocity(RT(8, 10), 4);` | UpdateSlide: TurnBodyToVelocity(8, 4) a0 |
| 154 | 80046e58 | slti 257 -> 370 (x1.44) | 903: `if (length > RT(0x100, 369))` | UpdateSlide friction: `length > 0x100` = slti 0x101 |
| 155 | 80046e6c | addiu 256 -> 369 (x1.44) | 904: `VecScaleToLength(&friction, length, RT(0x100, 369));` | UpdateSlide: VecScaleToLength a2 0x100 |
| 156 | 80046eb0 | slti 8961 -> 10753 (x1.2) | 909: `if (length > RT(0x2300, 10752)) {` | UpdateSlide: length > 0x2300 |
| 157 | 80046ec4 | addiu 8960 -> 10752 (x1.2) | 910: `VecScaleToLength(&ph->unk_0xe8, length, RT(0x2300, 10752));` | UpdateSlide: VecScaleToLength a2 |
| 158 | 80046ec8 | addiu 8960 -> 10752 (x1.2) | 746: `ph->m_SpeedAngle.m_Speed = RT(0x2300, 10752);` | UpdateSlide: m_Speed = 0x2300 |
| 159 | 80047058 | addiu 576 -> 829 (x1.44) | 936: `func_8003D92C(RT(0x240, 829), RT(0x180, 737));` | UpdateFlyingAbility: func_8003D92C a0 0x240 (hoisted into the beqz delay slot) |
| 160 | 80047138 | addiu 7680 -> 9216 (x1.2) | 935: `ph->m_TargetSpeedAngle.m_Speed = RT(0x1E00, 9216);` | UpdateFlyingAbility: target speed 0x1E00 |
| 161 | 80047148 | addiu 384 -> 737 (other) | 936: `func_8003D92C(RT(0x240, 829), RT(0x180, 737));` | UpdateFlyingAbility: func_8003D92C a1 0x180 ("other") |
| 162 | 8004723c | addiu -192 -> -276 (x1.44) | 941: `ph->m_Acceleration.z -= RT(0xC0, 276);` | UpdateFlyingAbility: Acceleration.z -= 0xC0 (then shared ClampFall tail) |
| 163 | 8004744c | slti -8960 -> -10752 (x1.2) | 759: `if (g_Spyro.m_Physics.m_Acceleration.z < RT(-0x2300, -10752))` | ClampFall: slti -0x2300 (delay-slot copies before `j .L80047450` in Hurt/Tumble25/Fall, and the shared tail in Glide); every ClampFall instance is a site |
| 164 | 80047454 | addiu -8960 -> -10752 (x1.2) | 760: `g_Spyro.m_Physics.m_Acceleration.z = RT(-0x2300, -10752);` | ClampFall: = -0x2300 |
| 165 | 80047504 | addiu 640 -> 768 (x1.2) | 1003: `func_8003D3B8(RT(0x280, 768));` | UpdateCircleMoby: func_8003D3B8(0x280) |
| 166 | 80047520 | addiu 32 -> 38 (x1.2) | 1005: `D_800758A0 += RT(0x20, 38);` | UpdateCircleMoby: D_800758A0 += 0x20 |
| 167 | 8004764c | addiu -192 -> -276 (x1.44) | 1024: `ph->m_Acceleration.z -= RT(0xC0, 276);` | UpdateCircleMoby: onEdge Acceleration.z -= 0xC0 |
| 168 | 800476a8 | addiu -192 -> -276 (x1.44) | 959: `drag.z = RT(-0xC0, -276);` | UpdateStop air: drag.z = -0xC0 (emitted on both arms of the length test) |
| 169 | 800476b8 | addiu -192 -> -276 (x1.44) | 959: `drag.z = RT(-0xC0, -276);` | UpdateStop air: drag.z = -0xC0 (emitted on both arms of the length test) |
| 170 | 800476dc | slti 257 -> 370 (x1.44) | 963: `if (length > RT(0x100, 369))` | UpdateStop ground: `length > 0x100` = slti 0x101 -> > 369 |
| 171 | 800476f0 | addiu 256 -> 369 (x1.44) | 964: `VecScaleToLength(&drag, length, RT(0x100, 369));` | UpdateStop ground: VecScaleToLength a2 0x100 |
| 172 | 80047728 | slti -8960 -> -10752 (x1.2) | 759: `if (g_Spyro.m_Physics.m_Acceleration.z < RT(-0x2300, -10752))` | ClampFall: shared tail after UpdateStop (also jumped to by UpdateFlyingAbility) |
| 173 | 80047730 | addiu -8960 -> -10752 (x1.2) | 760: `g_Spyro.m_Physics.m_Acceleration.z = RT(-0x2300, -10752);` | ClampFall: = -0x2300 |
| 174 | 800477e0 | addiu -192 -> -276 (x1.44) | 390: `g_Spyro.m_Physics.m_Acceleration.z = g_Spyro.m_Physics.m_TrueVelocity.z - RT(0xC0, 276);` | ApplyFall: shared tail .L800477D4 (Running/State3/Decay/Sidestep via ApplyGravity) |
| 175 | 80047a1c | slti 5120 -> 6144 (x1.2) | 1587: `if (ph->m_SpeedAngle.m_Speed < RT(0x1400, 6144)) {` | UpdateFlying: m_Speed < 0x1400 |
| 176 | 80047a24 | addiu 5120 -> 6144 (x1.2) | 1588: `ph->m_TargetSpeedAngle.m_Speed = RT(0x1400, 6144);` | UpdateFlying: target = 0x1400 |
| 177 | 80047a7c | addiu -5120 -> -6144 (x1.2) | 1598: `D_8007584C = ((ph->m_SpeedAngle.m_Speed - RT(0x1400, 6144)) >> 7) + 20;` | UpdateFlying rumble: (m_Speed - 0x1400) >> 7 (the +20, 0x50/0xA0 clamps are not sites) |

## Unpaired ranges

| # | range | verdict |
|---|---|---|
| R1 | replace US 80044080+1 / PAL 80049574+1 | noise: branch offset only; branch beqz 80047ae4->8004d12c (US/PAL targets hold the same code) |
| R2 | replace US 800440f4+1 / PAL 800495e8+1 | noise: branch offset only; branch bnez 8004773c->8004cd40 (US/PAL targets hold the same code) |
| R3 | replace US 80044108+1 / PAL 800495fc+1 | noise: branch offset only; branch bnez 8004773c->8004cd40 (US/PAL targets hold the same code) |
| R4 | replace US 80044340+1 / PAL 80049834+1 | noise: branch offset only; branch bnez 800477d4->8004cdd8 (US/PAL targets hold the same code) |
| R5 | replace US 80044354+1 / PAL 80049848+1 | noise: branch offset only; branch beqz 800477f4->8004cdf8 (US/PAL targets hold the same code) |
| R6 | replace US 80044640+1 / PAL 80049b34+1 | noise: branch offset only; branch bnez 800477d4->8004cdd8 (US/PAL targets hold the same code) |
| R7 | replace US 80044a4c+1 / PAL 80049f40+1 | noise: branch offset only; branch beqz 80047ae4->8004d12c (US/PAL targets hold the same code) |
| R8 | replace US 80044aa0+1 / PAL 80049f94+1 | noise: branch offset only; branch blez 80047ae4->8004d12c (US/PAL targets hold the same code) |
| R9 | replace US 80044bf0+1 / PAL 8004a0e4+1 | noise: branch offset only; branch beqz 80045040->8004a57c (US/PAL targets hold the same code) |
| R10 | replace US 80044e68+1 / PAL 8004a35c+1 | noise: branch offset only; branch beqz 80044f5c->8004a498 (US/PAL targets hold the same code) |
| R11 | replace US 80044e88+1 / PAL 8004a37c+1 | noise: branch offset only; branch bgez 80044eb0->8004a3c8 (the target is the start of the neighbouring changed root-scaling block, which is handled there) |
| R12 | replace US 80044e98+1 / PAL 8004a38c+1 | implemented at lines 639-641 (UpdateSuperchargeSpeed, + PalSpeed helper line 629): PAL multiplies the root by 4915 and >>12 (sll5/subu/sll2/subu/sll3/subu/sll2/addu chain = x4915) and uses base 9677 (0x1F80 x1.2). US: root + 0x1F80 / 0x1F80 - root. |
| R13 | insert US 80044ea0+0 / PAL 8004a394+9 | implemented at lines 639-641 (UpdateSuperchargeSpeed, + PalSpeed helper line 629): PAL multiplies the root by 4915 and >>12 (sll5/subu/sll2/subu/sll3/subu/sll2/addu chain = x4915) and uses base 9677 (0x1F80 x1.2). US: root + 0x1F80 / 0x1F80 - root. |
| R14 | replace US 80044ea4+1 / PAL 8004a3bc+1 | implemented at lines 639-641 (UpdateSuperchargeSpeed, + PalSpeed helper line 629): PAL multiplies the root by 4915 and >>12 (sll5/subu/sll2/subu/sll3/subu/sll2/addu chain = x4915) and uses base 9677 (0x1F80 x1.2). US: root + 0x1F80 / 0x1F80 - root. |
| R15 | replace US 80044eb8+1 / PAL 8004a3d0+10 | implemented at lines 639-641 (UpdateSuperchargeSpeed, + PalSpeed helper line 629): PAL multiplies the root by 4915 and >>12 (sll5/subu/sll2/subu/sll3/subu/sll2/addu chain = x4915) and uses base 9677 (0x1F80 x1.2). US: root + 0x1F80 / 0x1F80 - root. |
| R16 | replace US 80044ec0+1 / PAL 8004a3fc+1 | implemented at lines 639-641 (UpdateSuperchargeSpeed, + PalSpeed helper line 629): PAL multiplies the root by 4915 and >>12 (sll5/subu/sll2/subu/sll3/subu/sll2/addu chain = x4915) and uses base 9677 (0x1F80 x1.2). US: root + 0x1F80 / 0x1F80 - root. |
| R17 | replace US 800450ac+1 / PAL 8004a5e8+1 | noise: branch offset only; branch beqz 800477f4->8004cdf8 (US/PAL targets hold the same code) |
| R18 | replace US 8004525c+1 / PAL 8004a798+1 | noise: branch offset only; branch beqz 800463cc->8004b9d0 (US/PAL targets hold the same code) |
| R19 | replace US 80045274+1 / PAL 8004a7b0+1 | noise: branch offset only; branch beqz 80046184->8004b788 (US/PAL targets hold the same code) |
| R20 | replace US 800454b0+2 / PAL 8004a9ec+4 | implemented at lines 1300-1301 (FlyFree): pitchStep x-15 -> x-18 (negu, x9, x2) and turnStep x20 -> x24 (x3, <<3), both >>7. |
| R21 | replace US 800454bc+1 / PAL 8004aa00+1 | implemented at lines 1300-1301 (FlyFree): pitchStep x-15 -> x-18 (negu, x9, x2) and turnStep x20 -> x24 (x3, <<3), both >>7. |
| R22 | replace US 800454c4+1 / PAL 8004aa08+1 | implemented at lines 1300-1301 (FlyFree): pitchStep x-15 -> x-18 (negu, x9, x2) and turnStep x20 -> x24 (x3, <<3), both >>7. |
| R23 | replace US 80045600+1 / PAL 8004ab44+1 | noise: branch offset only; branch bgez 8004562c->8004ab90 (the target is the start of the neighbouring changed root-scaling block, which is handled there) |
| R24 | replace US 80045610+2 / PAL 8004ab54+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyFree instance): PAL root*4915>>5 / >>6 (US <<7 / <<6) on base 4992 (0x1040 x1.2). |
| R25 | insert US 8004561c+0 / PAL 8004ab5c+9 | implemented at lines 1225-1228 (SpeedFromHeight, FlyFree instance): PAL root*4915>>5 / >>6 (US <<7 / <<6) on base 4992 (0x1040 x1.2). |
| R26 | replace US 80045620+1 / PAL 8004ab84+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyFree instance): PAL root*4915>>5 / >>6 (US <<7 / <<6) on base 4992 (0x1040 x1.2). |
| R27 | replace US 80045634+2 / PAL 8004ab98+10 | implemented at lines 1225-1228 (SpeedFromHeight, FlyFree instance): PAL root*4915>>5 / >>6 (US <<7 / <<6) on base 4992 (0x1040 x1.2). |
| R28 | replace US 80045640+1 / PAL 8004abc4+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyFree instance): PAL root*4915>>5 / >>6 (US <<7 / <<6) on base 4992 (0x1040 x1.2). |
| R29 | replace US 800457c0+1 / PAL 8004ad44+1 | noise: branch offset only; branch beqz 80046184->8004b788 (US/PAL targets hold the same code) |
| R30 | replace US 80045844+1 / PAL 8004adc8+1 | noise: branch offset only; branch beqz 80046184->8004b788 (US/PAL targets hold the same code) |
| R31 | replace US 800459e0+1 / PAL 8004af64+1 | noise: branch offset only; branch bgez 80045a0c->8004afb0 (the target is the start of the neighbouring changed root-scaling block, which is handled there) |
| R32 | replace US 800459f0+2 / PAL 8004af74+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyLoop instance; same helper, identical PAL code). |
| R33 | insert US 800459fc+0 / PAL 8004af7c+9 | implemented at lines 1225-1228 (SpeedFromHeight, FlyLoop instance; same helper, identical PAL code). |
| R34 | replace US 80045a00+1 / PAL 8004afa4+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyLoop instance; same helper, identical PAL code). |
| R35 | replace US 80045a14+2 / PAL 8004afb8+10 | implemented at lines 1225-1228 (SpeedFromHeight, FlyLoop instance; same helper, identical PAL code). |
| R36 | replace US 80045a20+1 / PAL 8004afe4+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyLoop instance; same helper, identical PAL code). |
| R37 | replace US 80045a50+1 / PAL 8004b014+1 | noise: branch offset only; branch beqz 80045fb0->8004b5b4 (US/PAL targets hold the same code) |
| R38 | replace US 80045a64+1 / PAL 8004b028+1 | noise: branch offset only; branch bnez 80045fb0->8004b5b4 (US/PAL targets hold the same code) |
| R39 | replace US 80045d80+1 / PAL 8004b344+1 | noise: branch offset only; branch bgez 80045dac->8004b390 (the target is the start of the neighbouring changed root-scaling block, which is handled there) |
| R40 | replace US 80045d90+2 / PAL 8004b354+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyTurnAround instance; identical PAL code). |
| R41 | insert US 80045d9c+0 / PAL 8004b35c+9 | implemented at lines 1225-1228 (SpeedFromHeight, FlyTurnAround instance; identical PAL code). |
| R42 | replace US 80045da0+1 / PAL 8004b384+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyTurnAround instance; identical PAL code). |
| R43 | replace US 80045db4+2 / PAL 8004b398+10 | implemented at lines 1225-1228 (SpeedFromHeight, FlyTurnAround instance; identical PAL code). |
| R44 | replace US 80045dc0+1 / PAL 8004b3c4+1 | implemented at lines 1225-1228 (SpeedFromHeight, FlyTurnAround instance; identical PAL code). |
| R45 | replace US 800464e8+1 / PAL 8004baec+1 | implemented at lines 1140-1142 (UpdatePortalFlight target speed): US ((ahead*35)<<7)>>7, PAL ((ahead*21)<<8)>>7 = ahead*42 (x1.2), both + RT(0x1900, 7680). |
| R46 | replace US 800464f4+2 / PAL 8004baf8+2 | implemented at lines 1140-1142 (UpdatePortalFlight target speed): US ((ahead*35)<<7)>>7, PAL ((ahead*21)<<8)>>7 = ahead*42 (x1.2), both + RT(0x1900, 7680). |
| R47 | replace US 80046580+1 / PAL 8004bb84+1 | implemented at line 1145 (UpdatePortalFlight): turn*20>>7 -> turn*24>>7 (x3, <<3). |
| R48 | replace US 80046588+1 / PAL 8004bb8c+1 | implemented at line 1145 (UpdatePortalFlight): turn*20>>7 -> turn*24>>7 (x3, <<3). |
| R49 | replace US 80046ab8+1 / PAL 8004c0bc+1 | noise: branch offset only; branch beqz 80047ae4->8004d12c (US/PAL targets hold the same code) |
| R50 | replace US 80046d1c+1 / PAL 8004c320+1 | implemented at line 872 (UpdateWhirlwind top): below*4800>>10 -> below*5760>>10 (x3, x16-1=45, <<7). |
| R51 | replace US 80046d2c+1 / PAL 8004c330+1 | implemented at line 872 (UpdateWhirlwind top): below*4800>>10 -> below*5760>>10 (x3, x16-1=45, <<7). |
| R52 | replace US 80047450+1 / PAL 8004ca54+1 | noise: branch offset only; branch beqz 80047ae4->8004d12c (US/PAL targets hold the same code) |
| R53 | replace US 8004772c+1 / PAL 8004cd30+1 | noise: branch offset only; branch beqz 80047ae4->8004d12c (US/PAL targets hold the same code) |
| R54 | replace US 8004782c+1 / PAL 8004ce30+1 | noise: branch offset only; branch bgez 80047ae4->8004d12c (US/PAL targets hold the same code) |
| R55 | replace US 800479b4+1 / PAL 8004cfb8+1 | noise: branch offset only; branch bgez 800479d4->8004cffc (the target is the start of the neighbouring changed root-scaling block, which is handled there) |
| R56 | replace US 800479c4+2 / PAL 8004cfc8+11 | implemented at lines 1582-1584 (UpdateFlying, state 44): PAL root*4915>>12 (PalSpeed) added to/subtracted from the target speed; the PAL code reloads m_TargetSpeed after the multiply (scheduling only). |
| R57 | replace US 800479d0+1 / PAL 8004cff8+1 | implemented at lines 1582-1584 (UpdateFlying, state 44): PAL root*4915>>12 (PalSpeed) added to/subtracted from the target speed; the PAL code reloads m_TargetSpeed after the multiply (scheduling only). |
| R58 | replace US 800479dc+3 / PAL 8004d004+7 | implemented at lines 1582-1584 (UpdateFlying, state 44): PAL root*4915>>12 (PalSpeed) added to/subtracted from the target speed; the PAL code reloads m_TargetSpeed after the multiply (scheduling only). |
| R59 | insert US 800479ec+0 / PAL 8004d024+4 | implemented at lines 1582-1584 (UpdateFlying, state 44): PAL root*4915>>12 (PalSpeed) added to/subtracted from the target speed; the PAL code reloads m_TargetSpeed after the multiply (scheduling only). |
| R60 | replace US 800479f0+1 / PAL 8004d038+1 | implemented at lines 1582-1584 (UpdateFlying, state 44): PAL root*4915>>12 (PalSpeed) added to/subtracted from the target speed; the PAL code reloads m_TargetSpeed after the multiply (scheduling only). |

Totals: 178 sites; 176 rewritten as RT(), 2 (S116, S117) left unchanged (wing-trail model vertex, not timing). 60 ranges: 34 implemented, 26 noise, 0 left.
