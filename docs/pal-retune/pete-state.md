# R3 PAL retune map: func_8003EA68 and func_80041670

Files: `src/game/pete_state_hand.c` (func_8003EA68 and its Enter* helpers), `src/game/pete_transition_hand.c` (func_80041670). Line numbers are after the edit. Every site became `RT(us, pal)`; US expressions are unchanged.

Method: each site's US address was placed in its `switch` case with the asm jump tables (jtbl_80010F20, jtbl_80010FD8) and the field offset (g_Spyro+0x14C m_gravity, +0x118 m_Speed, +0x128 m_TrueSpeed, +0x114 TrueVelocity.z, +0x80 m_idleTimer, +0xFC Acceleration.z). An asm scan found every other instance of each retuned immediate. A helper literal (MovingState, LandingState, IsBlocked, EnterFall, EnterHurt, StartFastFlight, FellTooLong, JUMP_BOOST) changed only where all of its inlined copies are listed sites with the same PAL value. None needed splitting.


## func_8003EA68 constant sites (64)

| site | US addr | US -> PAL | file:line | how derived |
|---|---|---|---|---|
| 0 | 8003eb70 | addiu -128 -> -184 (x1.44) | pete_state_hand.c:444 | case 0 gravity -0x80 (li) |
| 1 | 8003ec10 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:166 | EnterRunning gravity; asm has 3 copies (branches of `== 3` test), one literal |
| 2 | 8003ec24 | slti 5121 -> 6145 (x1.2) | pete_state_hand.c:164 | EnterRunning `m_Speed > 0x1400` is slti 0x1401; PAL slti 6145 -> `> 0x1800` |
| 3 | 8003ec2c | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:166 | EnterRunning gravity; asm has 3 copies (branches of `== 3` test), one literal |
| 4 | 8003ec30 | addiu 5120 -> 6144 (x1.2) | pete_state_hand.c:165 | EnterRunning clamp value 0x1400 -> 0x1800 |
| 5 | 8003ec38 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:166 | EnterRunning gravity; asm has 3 copies (branches of `== 3` test), one literal |
| 6 | 8003edf4 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:453 | case 4 (jtbl .L8003EDF4) gravity |
| 7 | 8003ee20 | addiu 3520 -> 4224 (x1.2) | pete_state_hand.c:459 | case 5 unk_0xe8.z (g_Spyro+0xF0) |
| 8 | 8003ee2c | addiu -192 -> -276 (x1.44) | pete_state_hand.c:460 | case 5 gravity (g_Spyro+0x14C) |
| 9 | 8003eec4 | addiu -192 -> -276 (x1.44) | pete_state_hand.c:336 | EnterFall gravity; inlined for cases 6 and 16, 3 delay-slot copies each, all -276 |
| 10 | 8003eedc | addiu -192 -> -276 (x1.44) | pete_state_hand.c:336 | EnterFall gravity; inlined for cases 6 and 16, 3 delay-slot copies each, all -276 |
| 11 | 8003eeec | addiu -192 -> -276 (x1.44) | pete_state_hand.c:336 | EnterFall gravity; inlined for cases 6 and 16, 3 delay-slot copies each, all -276 |
| 12 | 8003ef24 | addiu -192 -> -276 (x1.44) | pete_state_hand.c:336 | EnterFall gravity; inlined for cases 6 and 16, 3 delay-slot copies each, all -276 |
| 13 | 8003ef3c | addiu -192 -> -276 (x1.44) | pete_state_hand.c:336 | EnterFall gravity; inlined for cases 6 and 16, 3 delay-slot copies each, all -276 |
| 14 | 8003ef4c | addiu -192 -> -276 (x1.44) | pete_state_hand.c:336 | EnterFall gravity; inlined for cases 6 and 16, 3 delay-slot copies each, all -276 |
| 15 | 8003ef68 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:498 | case 9/10 (jtbl .L8003EF50) gravity |
| 16 | 8003efe4 | slti 8064 -> 9677 (x1.2) | pete_state_hand.c:214 | StartFastFlight `speed < 0x1F80` (slti 0x1F80); inlined in EnterCharge, EnterFlyingAbility, EnterFlying, all 9677 |
| 17 | 8003eff0 | addiu 8064 -> 9677 (x1.2) | pete_state_hand.c:214 | StartFastFlight clamp value 0x1F80; same three instances |
| 18 | 8003f010 | addiu -576 -> -829 (x1.44) | pete_state_hand.c:230 | EnterCharge gravity (after m_TurnMomentum 0x148 = 0) |
| 19 | 8003f2a4 | addiu -768 -> -1106 (x1.44) | pete_state_hand.c:256 | EnterKnockback gravity |
| 20 | 8003f2e4 | slti 15 -> 13 (x5/6) | pete_state_hand.c:259 | EnterKnockback Rumble(&D_800757D0, 0xF): slti in Rumble, at the call site |
| 21 | 8003f2ec | addiu 15 -> 13 (x5/6) | pete_state_hand.c:259 | same Rumble call, the stored value |
| 22 | 8003f304 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:509 | case 13 (jtbl .L8003F300) gravity, `j .L8003FC64` delay slot |
| 23 | 8003f340 | slti 7681 -> 9217 (x1.2) | pete_state_hand.c:272 | EnterHurt `len > 0x1E00` is slti 0x1E01; PAL 9217 -> `> 0x2400`; inlined for cases 7 and 14 |
| 24 | 8003f350 | addiu 7680 -> 9216 (x1.2) | pete_state_hand.c:273 | EnterHurt VecScaleToLength length 0x1E00 -> 0x2400; cases 7 and 14 |
| 25 | 8003f35c | addiu -768 -> -1106 (x1.44) | pete_state_hand.c:274 | EnterHurt gravity; cases 7 and 14 |
| 26 | 8003f3ac | slti 45 -> 38 (x5/6) | pete_state_hand.c:482 | case 7 Rumble(&D_80075764, 0x2D) (slti and stored value) |
| 27 | 8003f3b4 | addiu 45 -> 38 (x5/6) | pete_state_hand.c:482 | case 7 Rumble(&D_80075764, 0x2D) (slti and stored value) |
| 28 | 8003f400 | slti 7681 -> 9217 (x1.2) | pete_state_hand.c:272 | EnterHurt `len > 0x1E00` is slti 0x1E01; PAL 9217 -> `> 0x2400`; inlined for cases 7 and 14 |
| 29 | 8003f410 | addiu 7680 -> 9216 (x1.2) | pete_state_hand.c:273 | EnterHurt VecScaleToLength length 0x1E00 -> 0x2400; cases 7 and 14 |
| 30 | 8003f41c | addiu -768 -> -1106 (x1.44) | pete_state_hand.c:274 | EnterHurt gravity; cases 7 and 14 |
| 31 | 8003f46c | slti 15 -> 13 (x5/6) | pete_state_hand.c:486 | case 14 Rumble(&D_80075904, 0xF) (slti and stored value) |
| 32 | 8003f474 | addiu 15 -> 13 (x5/6) | pete_state_hand.c:486 | case 14 Rumble(&D_80075904, 0xF) (slti and stored value) |
| 33 | 8003f4c4 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:552 | case 25 m_Acceleration.z (g_Spyro+0xFC) = -0xC00 |
| 34 | 8003f4f0 | slti 15 -> 13 (x5/6) | pete_state_hand.c:555 | case 25 Rumble(&D_80075904, 0xF); asm tail .L8003F4E4 is case 25 only |
| 35 | 8003f4f8 | addiu 15 -> 13 (x5/6) | pete_state_hand.c:555 | case 25 Rumble(&D_80075904, 0xF); asm tail .L8003F4E4 is case 25 only |
| 36 | 8003f548 | addiu -768 -> -1106 (x1.44) | pete_state_hand.c:570 | case 27 (jtbl .L8003F520) gravity |
| 37 | 8003f5fc | slti 6401 -> 7681 (x1.2) | pete_state_hand.c:300 | EnterGlide `m_Speed > 0x1900` (slti 0x1901) -> `> 0x1E00` |
| 38 | 8003f604 | addiu 6400 -> 7680 (x1.2) | pete_state_hand.c:301 | EnterGlide clamp 0x1900 -> 0x1E00 |
| 39 | 8003f61c | slti 1920 -> 2304 (x1.2) | pete_state_hand.c:302 | EnterGlide `m_Speed < 0x780` -> `< 0x900` |
| 40 | 8003f624 | addiu 1920 -> 2304 (x1.2) | pete_state_hand.c:303 | EnterGlide clamp 0x780 -> 0x900 |
| 41 | 8003f6a8 | addiu -128 -> -184 (x1.44) | pete_state_hand.c:307 | EnterGlide gravity, two delay-slot copies (unk_0x254 branches) |
| 42 | 8003f6b8 | addiu -128 -> -184 (x1.44) | pete_state_hand.c:307 | EnterGlide gravity, two delay-slot copies (unk_0x254 branches) |
| 43 | 8003f74c | addiu 128 -> 184 (x1.44) | pete_state_hand.c:328 | EnterWhirlwind gravity +0x80, three copies (AngleDelta branches / no-moby branch) |
| 44 | 8003f75c | addiu 128 -> 184 (x1.44) | pete_state_hand.c:328 | EnterWhirlwind gravity +0x80, three copies (AngleDelta branches / no-moby branch) |
| 45 | 8003f768 | addiu 128 -> 184 (x1.44) | pete_state_hand.c:328 | EnterWhirlwind gravity +0x80, three copies (AngleDelta branches / no-moby branch) |
| 46 | 8003f79c | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:522 | case 18/36-43 (jtbl .L8003F784) gravity (v1) |
| 47 | 8003f7e4 | addiu -512 -> -737 (x1.44) | pete_state_hand.c:530 | case 19 gravity -0x200 |
| 48 | 8003f884 | slti 8064 -> 9677 (x1.2) | pete_state_hand.c:214 | StartFastFlight `speed < 0x1F80` (slti 0x1F80); inlined in EnterCharge, EnterFlyingAbility, EnterFlying, all 9677 |
| 49 | 8003f890 | addiu 8064 -> 9677 (x1.2) | pete_state_hand.c:214 | StartFastFlight clamp value 0x1F80; same three instances |
| 50 | 8003f8d0 | addiu -576 -> -829 (x1.44) | pete_state_hand.c:357 | EnterFlyingAbility gravity (.L8003F8D0) |
| 51 | 8003f990 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:561 | case 26 gravity; KeepForwardSpeed inlined, two branch copies |
| 52 | 8003f9a4 | addiu -3072 -> -4424 (x1.44) | pete_state_hand.c:561 | case 26 gravity; KeepForwardSpeed inlined, two branch copies |
| 53 | 8003f9b8 | addiu 640 -> 768 (x1.2) | pete_state_hand.c:563 | case 26 unk_0x144 (g_Spyro+0x144) 0x280 -> 0x300 |
| 54 | 8003fa54 | addiu -768 -> -1106 (x1.44) | pete_state_hand.c:391 | EnterThrown gravity (cases 22/28, after Atan2) |
| 55 | 8003fac8 | addiu -128 -> -184 (x1.44) | pete_state_hand.c:403 | EnterSplash gravity (case 29 .L8003FAB4) |
| 56 | 8003fb88 | slti 15 -> 13 (x5/6) | pete_state_hand.c:397, 410, 573 | shared tail .L8003FB6C/.L8003FB7C: Rumble(&D_80075904, 0xF) of EnterThrown (22/28), EnterSplash (29) and case 27; each hand-C call gets RT(0xF, 13) |
| 57 | 8003fb90 | addiu 15 -> 13 (x5/6) | pete_state_hand.c:397, 410, 573 | shared tail .L8003FB6C/.L8003FB7C: Rumble(&D_80075904, 0xF) of EnterThrown (22/28), EnterSplash (29) and case 27; each hand-C call gets RT(0xF, 13) |
| 58 | 8003fbc4 | addiu -768 -> -1106 (x1.44) | pete_state_hand.c:419 | EnterDying gravity, case 30 instance (.L8003FBA4) |
| 59 | 8003fc14 | addiu -768 -> -1106 (x1.44) | pete_state_hand.c:419 | EnterDying gravity, case 31 instance (.L8003FBF4); same literal |
| 60 | 8003fc60 | addiu -128 -> -184 (x1.44) | pete_state_hand.c:585 | case 35 (.L8003FC60) gravity, tail shared with case 13 |
| 61 | 8003fcd0 | slti 8064 -> 9677 (x1.2) | pete_state_hand.c:214 | StartFastFlight `speed < 0x1F80` (slti 0x1F80); inlined in EnterCharge, EnterFlyingAbility, EnterFlying, all 9677 |
| 62 | 8003fcdc | addiu 8064 -> 9677 (x1.2) | pete_state_hand.c:214 | StartFastFlight clamp value 0x1F80; same three instances |
| 63 | 8003fcf8 | addiu -576 -> 43 (other) | pete_state_hand.c:373 | EnterFlying gravity. PAL's paired immediate (43) is a level-id compare it inserted; the real PAL gravity is `li v0,-829` at 800451ec (delay slot), so RT(-0x240, -829). PAL's extra `g_LevelId == 43 -> -3840` is NOT ported (see ranges) |

## func_8003EA68 unpaired ranges (24)

| range (US / PAL) | verdict |
|---|---|
| 8003eb20+1 / 80043fa0+1 | noise: branch target relocation (beqz to end) |
| 8003edc8+1 / 80044248+1 | noise: branch target |
| insert 8003ee94+0 / 80044314+6 | NOT implemented: PAL case 5 (jump) ends with PlaySound(g_Spu.m_SoundTable byte 0x42, &g_Spyro, 16, NULL) after the D_800756B4 store. That is a new sound, not a retuned constant. A comment marks it at pete_state_hand.c:466 |
| 8003ee98+1 / 80044330+1 | noise: tail of the same insert (j PlaySound with a3=0) |
| 8003eec0+1 / 80044358+1 | noise: branch target |
| 8003eed8+1 / 80044370+1 | noise: branch target |
| 8003ef20+1 / 800443b8+1 | noise: branch target |
| 8003ef38+1 / 800443d0+1 | noise: branch target |
| insert 8003ef50+0 / 800443e8+13 | NOT implemented: PAL's jump table sends state 8 to new code (0x800443e8): walkingState=0, unk_0x15c=1, then PlaySound(table byte 0x42, &g_Spyro, 16, NULL). US case 8 plays nothing. A new sound, not timing. A comment marks it at pete_state_hand.c:492 |
| 8003f094+1 / 80044560+1 | noise: branch target |
| 8003f0b4+1 / 80044580+1 | noise: branch target |
| 8003f26c+3 / 80044738+3 | noise: register allocation (s0 -> s2) in EnterKnockback |
| 8003f280+2 / 8004474c+2 | noise: same register change |
| 8003f2e8+1 / 800447b4+1 | noise: branch target (now to the inserted sound below) |
| insert 8003f2f8+0 / 800447c4+5 | NOT implemented: PAL EnterKnockback (case 12) ends with PlaySound(table byte 0x43, &g_Spyro, 16, NULL). A new sound, not timing. A comment marks it at pete_state_hand.c:260 |
| 8003f2fc+1 / 800447dc+1 | noise: tail of the same insert |
| 8003f3b0+1 / 80044890+1 | noise: branch target |
| 8003fb8c+1 / 8004506c+1 | noise: branch target |
| 8003fce8+2 / 800451c8+2 | part of the next change: PAL loads `lw v1, 0x8007C23C` (g_LevelId; US 0x8007596C, by the offsets of neighbouring globals) for the compare below |
| 8003fcf4+1 / 800451d4+2 | same: `li v0,43`. Site 63 pairs US -0x240 with this 43 |
| insert 8003fd04+0 / 800451e8+3 | partly implemented: the gravity becomes RT(-0x240, -829) at pete_state_hand.c:373. NOT implemented: PAL EnterFlying (state 44) sets gravity -3840 instead when g_LevelId == 43 (Tree Tops, if the ids follow world*10+n). -3840 is not a scaled -0x240, so it is a level-specific behaviour change. A comment marks it at pete_state_hand.c:371 |
| 8003fd1c+1 / 8004520c+3 | noise: register allocation (a3 holds &g_Spyro+0x15C) |
| 8003fd30+2 / 80045228+1 | noise: same register change |
| 8003fd44+1 / 80045238+1 | noise: same (a3+0x145 = g_Spyro+0x2A1, US s0+0x225 is the same address) |

## func_80041670 constant sites (89)

| site | US addr | US -> PAL | file:line | how derived |
|---|---|---|---|---|
| 0 | 80041744 | slti 1025 -> 1230 (x1.2) | pete_transition_hand.c:223 | FromStanding (case 0) `m_TrueSpeed > 0x400` slti 0x401; PAL 1230 -> `> 1229` |
| 1 | 800418d4 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 2 | 800418dc | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 3 | 80041a2c | slti 1600 -> 1920 (x1.2) | pete_transition_hand.c:277 | FromRunning `speed < 0x640` -> `< 0x780` |
| 4 | 80041a38 | slti 3200 -> 3840 (x1.2) | pete_transition_hand.c:279 | FromRunning `speed < 0xC80` -> `< 0xF00` |
| 5 | 80041b08 | slti 256 -> 307 (x1.2) | pete_transition_hand.c:295 | FromStopping `m_TrueSpeed < 0x100` -> `< 307` |
| 6 | 80041b34 | slti 18 -> 15 (x5/6) | pete_transition_hand.c:299 | FromStopping `m_idleTimer < 18` -> `< 15` |
| 7 | 80041b70 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 8 | 80041b9c | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 9 | 80041c40 | slti 37 -> 31 (x5/6) | pete_transition_hand.c:311 | FromTurning `m_idleTimer < 37` -> `< 31` |
| 10 | 80041c60 | addiu 2560 -> 3072 (x1.2) | pete_transition_hand.c:315 | FromTurning m_Speed = 0xA00 -> 0xC00 |
| 11 | 80041d38 | slti 16 -> 14 (x5/6) | pete_transition_hand.c:332 | FromJumping `m_idleTimer >= 16` (slti 0x10) -> `>= 14` |
| 12 | 80041de0 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 13 | 80041de8 | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 14 | 80041e10 | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 15 | 80041e4c | addiu 8064 -> 9677 (x1.2) | pete_transition_hand.c:341 | FromJumping m_Speed = 0x1F80 -> 9677 |
| 16 | 80041ed8 | slti -6400 -> -7680 (x1.2) | pete_transition_hand.c:346 | FromJumping `vz < -0x1900` -> `< -0x1E00` |
| 17 | 80041ef0 | slti 121 -> 101 (x5/6) | pete_transition_hand.c:346 | FromJumping `m_idleTimer > 120` (slti 0x79) ; PAL 101 -> `> 100` |
| 18 | 80041fcc | slti 16 -> 14 (x5/6) | pete_transition_hand.c:362 | FromAirborne glide wait for 32 (a0=0x20 path) `>= 16` -> `>= 14` |
| 19 | 80042000 | slti 31 -> 27 (x5/6) | pete_transition_hand.c:362 | FromAirborne glide wait for 15 (a0=0xF path) `>= 31` -> `>= 27` (PAL value as found; 5/6 would be 26) |
| 20 | 800420b0 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 21 | 800420b8 | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 22 | 80042164 | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 23 | 80042210 | slti 301 -> 251 (x5/6) | pete_transition_hand.c:157 | FellTooLong `m_idleTimer <= 300` (slti 0x12D); PAL 251 -> `<= 250`. asm has 2 copies (cases 6, 19; case 16 reuses one), both listed |
| 24 | 8004239c | slti 21 -> 18 (x5/6) | pete_transition_hand.c:409 | FromSidestep `m_idleTimer <= 20` (slti 0x15); PAL 18 -> `<= 17`; cases 9 and 10 |
| 25 | 80042430 | slti 21 -> 18 (x5/6) | pete_transition_hand.c:409 | FromSidestep `m_idleTimer <= 20` (slti 0x15); PAL 18 -> `<= 17`; cases 9 and 10 |
| 26 | 800424ac | addiu 3520 -> 4224 (x1.2) | pete_transition_hand.c:57 | JUMP_BOOST macro 0xDC0 -> 4224 (FromCharging, FromSuperchargeJump, FromSupercharge; all three listed) |
| 27 | 80042548 | slti 3201 -> 3841 (x1.2) | pete_transition_hand.c:120 | IsBlocked `speed > 0xC80` (slti 0xC81) ; PAL 3841 -> `> 0xF00`; cases 11, 20, 44, all listed |
| 28 | 800425a8 | slti 12289 -> 14747 (x1.2) | pete_transition_hand.c:446 | FromCharging `m_Speed > 0x3000` (slti 0x3001); PAL 14747 -> `> 14746` |
| 29 | 800425f0 | slti 8064 -> 9677 (x1.2) | pete_transition_hand.c:452 | FromCharging `speed < 0x1F80` -> `< 9677` |
| 30 | 800425fc | addiu 8064 -> 9677 (x1.2) | pete_transition_hand.c:452 | FromCharging clamp 0x1F80 -> 9677 |
| 31 | 80042660 | slti 17 -> 14 (x5/6) | pete_transition_hand.c:461 | FromKnockback `m_idleTimer > 16` (slti 0x11); PAL 14 -> `> 13` |
| 32 | 800426ac | slti 25 -> 21 (x5/6) | pete_transition_hand.c:463 | FromKnockback `m_idleTimer > 24` (slti 0x19); PAL 21 -> `> 20` |
| 33 | 80042794 | slti 31 -> 26 (x5/6) | pete_transition_hand.c:217 | FromStanding state-13 path `m_idleTimer < 31` -> `< 26` |
| 34 | 800427e0 | slti 24 -> 20 (x5/6) | pete_transition_hand.c:786 | case 7 EndHurt(24): `m_idleTimer < pTicks` slti 0x18 -> 20, RT at the call site |
| 35 | 80042814 | slti 24 -> 20 (x5/6) | pete_transition_hand.c:792 | case 14/22/28 EndHurt(24) -> 20 |
| 36 | 80042848 | slti 48 -> 40 (x5/6) | pete_transition_hand.c:793 | case 27 EndHurt(48) -> 40 |
| 37 | 800428cc | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 38 | 800428d4 | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 39 | 800428fc | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 40 | 80042938 | slti 1024 -> 853 (x5/6) | pete_transition_hand.c:474 | FromGlide `m_TrueSpeed < 0x400` -> `< 853`; cases 15 and 23 |
| 41 | 80042968 | slti 17 -> 14 (x5/6) | pete_transition_hand.c:479 | FromGlide (15) `m_idleTimer > 16` -> `> 13` |
| 42 | 80042a74 | slti 3200 -> 3840 (x1.2) | pete_transition_hand.c:488 | FromGlide (15) `m_Speed >= 0xC80` (slti 0xC80) -> `>= 0xF00` |
| 43 | 80042a8c | slti 17 -> 14 (x5/6) | pete_transition_hand.c:488 | FromGlide (15) `m_idleTimer <= 16` (slti 0x11) -> `<= 13` |
| 44 | 80042b58 | slti 1025 -> 1230 (x1.2) | pete_transition_hand.c:534 | FromIdleAnimation `m_TrueSpeed > 0x400` -> `> 1229` |
| 45 | 80042c9c | slti 24 -> 20 (x5/6) | pete_transition_hand.c:506 | FromDrop `m_idleTimer < 24` -> `< 20` |
| 46 | 80042d48 | slti 16 -> 14 (x5/6) | pete_transition_hand.c:520 | FromDrop `m_idleTimer >= 16` -> `>= 14` |
| 47 | 80042dc0 | slti 7937 -> 9524 (x1.2) | pete_transition_hand.c:564 | FromSliding `m_Speed > 0x1F00` (slti 0x1F01); PAL 9524 -> `> 9523` |
| 48 | 80042e24 | slti 2304 -> 2765 (x1.2) | pete_transition_hand.c:570 | FromSliding `m_TrueSpeed < 0x900` -> `< 2765` |
| 49 | 80042e50 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 50 | 80042e58 | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 51 | 80042e80 | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 52 | 80042ea8 | slti 301 -> 251 (x5/6) | pete_transition_hand.c:157 | FellTooLong `m_idleTimer <= 300` (slti 0x12D); PAL 251 -> `<= 250`. asm has 2 copies (cases 6, 19; case 16 reuses one), both listed |
| 53 | 8004300c | slti 3201 -> 3841 (x1.2) | pete_transition_hand.c:120 | IsBlocked `speed > 0xC80` (slti 0xC81) ; PAL 3841 -> `> 0xF00`; cases 11, 20, 44, all listed |
| 54 | 80043178 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:604 | FromTrotting `> 0xF00` -> `> 0x1200` |
| 55 | 80043180 | slti 1600 -> 1920 (x1.2) | pete_transition_hand.c:606 | FromTrotting `< 0x640` -> `< 0x780` |
| 56 | 800431f8 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 57 | 80043200 | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 58 | 80043228 | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 59 | 80043264 | slti 1024 -> 853 (x5/6) | pete_transition_hand.c:474 | FromGlide `m_TrueSpeed < 0x400` -> `< 853`; cases 15 and 23 |
| 60 | 800432cc | slti 3201 -> 3841 (x1.2) | pete_transition_hand.c:497 | FromGlide (23) `m_Speed > 0xC80` (slti 0xC81) -> `> 0xF00` |
| 61 | 80043364 | addiu 3520 -> 4224 (x1.2) | pete_transition_hand.c:57 | JUMP_BOOST macro 0xDC0 -> 4224 (FromCharging, FromSuperchargeJump, FromSupercharge; all three listed) |
| 62 | 80043530 | slti 24 -> 20 (x5/6) | pete_transition_hand.c:790 | case 25 EndHurt(24) -> 20 |
| 63 | 8004373c | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 64 | 80043744 | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 65 | 8004378c | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 66 | 800437e0 | slti 1024 -> 853 (x5/6) | pete_transition_hand.c:680 | FromFlightGlide `m_TrueSpeed < 0x400` -> `< 853`; cases 32, 33, 34 |
| 67 | 800437f8 | slti 2049 -> 2459 (x1.2) | pete_transition_hand.c:683 | FromFlightGlide `vz > 0x800` (slti 0x801); PAL 2459 -> `> 2458` |
| 68 | 80043818 | slti -4096 -> -4915 (x1.2) | pete_transition_hand.c:685 | FromFlightGlide `vz < -0x1000` -> `< -4915` |
| 69 | 80043910 | slti 17 -> 14 (x5/6) | pete_transition_hand.c:703 | FromFlightGlide `m_idleTimer > 16` (slti 0x11) -> `> 13`; cases 32, 33, 34 |
| 70 | 80043998 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 71 | 800439a0 | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 72 | 800439e8 | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 73 | 80043a3c | slti 1024 -> 853 (x5/6) | pete_transition_hand.c:680 | FromFlightGlide `m_TrueSpeed < 0x400` -> `< 853`; cases 32, 33, 34 |
| 74 | 80043a54 | slti 31 -> 26 (x5/6) | pete_transition_hand.c:689 | FromFlightGlide `m_idleTimer >= 31` -> `>= 26`; cases 33 and 34 (one literal) |
| 75 | 80043a6c | slti 1536 -> 1843 (x1.2) | pete_transition_hand.c:689 | FromFlightGlide (33) `vz < 0x600` -> `< 1843` |
| 76 | 80043b4c | slti 17 -> 14 (x5/6) | pete_transition_hand.c:703 | FromFlightGlide `m_idleTimer > 16` (slti 0x11) -> `> 13`; cases 32, 33, 34 |
| 77 | 80043bd4 | slti 3841 -> 4609 (x1.2) | pete_transition_hand.c:92 | MovingState `speed > 0xF00` (slti 0xF01); PAL 4609 -> `> 0x1200`. Inlined in cases 1,3,5,6,15,19,23,32,33,34; all listed, all 4609 |
| 78 | 80043bdc | slti 1921 -> 2305 (x1.2) | pete_transition_hand.c:94 | MovingState `speed > 0x780` (slti 0x781); PAL 2305 -> `> 0x900`; same instances |
| 79 | 80043c24 | slti 1920 -> 2304 (x1.2) | pete_transition_hand.c:104 | LandingState `m_Speed < 0x780` -> `< 0x900`; inlined in cases 5,6,15,19,23,32,33,34; all listed |
| 80 | 80043c78 | slti 1024 -> 853 (x5/6) | pete_transition_hand.c:680 | FromFlightGlide `m_TrueSpeed < 0x400` -> `< 853`; cases 32, 33, 34 |
| 81 | 80043cb8 | slti 31 -> 26 (x5/6) | pete_transition_hand.c:689 | FromFlightGlide `m_idleTimer >= 31` -> `>= 26`; cases 33 and 34 (one literal) |
| 82 | 80043cd0 | slti -3071 -> -3685 (x1.2) | pete_transition_hand.c:689 | FromFlightGlide (34) `vz >= -0xBFF` (slti -0xBFF); PAL -3685 -> `>= -3685` |
| 83 | 80043db0 | slti 17 -> 14 (x5/6) | pete_transition_hand.c:703 | FromFlightGlide `m_idleTimer > 16` (slti 0x11) -> `> 13`; cases 32, 33, 34 |
| 84 | 80043e3c | slti 10 -> 8 (x5/6) | pete_transition_hand.c:717 | FromSupercharge `D_80075700 >= 10` (slti 0xA) -> `>= 8` |
| 85 | 80043eb0 | addiu 3520 -> 4224 (x1.2) | pete_transition_hand.c:57 | JUMP_BOOST macro 0xDC0 -> 4224 (FromCharging, FromSuperchargeJump, FromSupercharge; all three listed) |
| 86 | 80043f24 | slti 15 -> 13 (x5/6) | pete_transition_hand.c:736 | FromSupercharge `m_idleTimer >= 15` -> `>= 13` |
| 87 | 80043f68 | slti 3201 -> 3841 (x1.2) | pete_transition_hand.c:120 | IsBlocked `speed > 0xC80` (slti 0xC81) ; PAL 3841 -> `> 0xF00`; cases 11, 20, 44, all listed |
| 88 | 80043fb0 | slti 7680 -> 9216 (x1.2) | pete_transition_hand.c:740 | FromSupercharge `m_Speed < 0x1E00` -> `< 0x2400` |

## func_80041670 unpaired ranges (0)

None.


## Unchanged literals checked (US instances not listed, so PAL keeps them)

- func_80041670: FromWalking `m_idleTimer >= 16` (800418bc), FromCirclingMoby `< 16` (80043628), FromSinking `>= 16` (80043678) and `> 60`, FromState8 13/20/0x15, EndHurt pad lock 12, FromDrop pad lock 8, IsFalling airTime 4, FromSliding airTime 8.
- func_8003EA68: EnterKnockback Rumble(&D_8007584C, 0x78); FindChargeTarget distances; idle wait `- 2`.

