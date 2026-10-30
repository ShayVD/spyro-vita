# Translation log

One entry per function translated from asm to C: function, method, how it was verified, known differences.

**mips2c v0** (`tools/mips2c/mips2c.py`, M1): one C function per `.s` file, every glabel an entry; registers as locals, `$sp` on the emulated stack in the arena; every load and store maps the KUSEG RAM mirror (0x00000000-0x001FFFFF) onto the arena; computed jumps dispatch on PS1 code addresses. "Ran without fault" means no crash, no `mips2c_bad_jump` and no PsyCross parse error in `logs/run1-final3` (title and menu, 2600 vblanks) and `logs/run1-demo4` (12000 vblanks, two attract demos). It is not proof of equivalence. "Differential test" means `tests/m2c_diff` replayed calls captured in PCSX-Redux (porting-plan §7.3, decision D22) against the translation and compared registers, GTE, scratchpad and memory.

| Function | Method | Verified by | Notes |
|---|---|---|---|
| `Memset` | hand (`asm/memory.s`), src/game/memory.c | Read against the asm. Called in run1 (no fault). | Same stop condition as the asm (size must be a non-zero multiple of 4). |
| `Memset16` | hand (`asm/memory.s`), src/game/memory.c | Read against the asm. Called in run1 (no fault). | Size must be a non-zero multiple of 16. |
| `Memcpy` | hand (`asm/memory.s`), src/game/memory.c | Read against the asm. Called in run1 (no fault). | Copies word by word; no source over-read. |
| `func_80016784` | hand (`asm/order_table.s`), src/game/order_table.c | Read against the asm. Called in run1 (no fault). | Terminator D_8006BBE0 copied into the arena at its PS1 address (24-bit OT link). |
| `func_800168A0` | hand (`asm/order_table.s`), src/game/order_table.c | Read against the asm. Called in run1 (no fault). |  |
| `func_800168DC` | hand (`asm/order_table.s`), src/game/order_table.c | Read against the asm. Called in run1 (no fault). |  |
| `rand`, `srand` | hand (`asm/psyq.s`, PsyQ libc), src/game/rand_psyq.c | Read against the asm. Called in run1 (no fault). | PsyQ LCG; seed 0 at boot, as its BSS word. |
| `func_80017FD4` | hand (`asm/system_time.s`), src/game/system_time.c | Read against the asm. Called in run1 (no fault). | Root counter 2 derived from the vblank count (4,233,600 Hz / 59.94), deterministic; only seeds the title fairy. |
| `Atan2Fast` | mips2c v0, unit `asm/math.s` (1322 insns) | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | GTE via PsyCross |
| `Atan2` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `Sin` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `Cos` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80016D08` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `RotVec8ToMatrix` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80016FD0` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `VecRotateByMatrix` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecRotateByLastMatrix` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecRotateByCam` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017188` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `VecMagnitude` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecRefineMagnitude` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017330` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017428` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 10/10 cases match PCSX-Redux. | see first row of unit |
| `func_80017548` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `VecScaleToLength` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017614` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `VecShiftLeft` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecShiftRight` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecNull` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecCopy` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_8001771C` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `VecAdd` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecSub` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecMult` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_800177F8` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 4/4 cases match PCSX-Redux. | see first row of unit |
| `func_80017854` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `func_80017894` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `func_80017908` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017928` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017948` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `func_8001796C` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `OctDistance` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_800179F0` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017A38` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017AA4` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017B48` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `func_80017BFC` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017C24` | mips2c v0, unit `asm/math.s` | Not called in the 9000-vblank oracle run (no differential cases). Ran without fault in run1. | see first row of unit |
| `func_80017C4C` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `VecToShortVec` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `func_80017C84` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20/20 cases match PCSX-Redux. | see first row of unit |
| `ColTriUnpack` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 20 cases incomplete, it reads level data through `g_Environment` outside the captured pages. Ran without fault in run1. | see first row of unit |
| `func_80017D7C` | mips2c v0, unit `asm/math.s` | Differential test (tests/m2c_diff, `logs/m2c-math1`): 1/1 cases match PCSX-Redux. | see first row of unit |
| `MobyAlloc` | mips2c v0, unit `asm/42CC4.s` (1067 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | 4 packed-pointer rebase(s); scratchpad (2 bases); GTE via PsyCross |
| `MobyFree` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `MobyUpdateCollision` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `MobySetShaded` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_800529E4` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `MobyGetAnimatedVertex` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80052F38` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_800530C0` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `MobyUpdateShadow` | mips2c v0, unit `asm/42CC4.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80051FEC` | mips2c v0, unit `asm/moby_lists.s` (310 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | scratchpad (1 bases); GTE via PsyCross |
| `func_800521C0` | mips2c v0, unit `asm/moby_lists.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_800522C0` | mips2c v0, unit `asm/moby_lists.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80053570` | mips2c v0, unit `asm/particle_alloc.s` (77 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80053608` | mips2c v0, unit `asm/particle_alloc.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `ColorLerp` | mips2c v0, unit `asm/images.s` (96 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | GTE via PsyCross |
| `func_80017E98` | mips2c v0, unit `asm/images.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80017F24` | mips2c v0, unit `asm/images.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `ColTraceRay` | mips2c v0, unit `asm/collision.s` (3932 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | 4 packed-pointer rebase(s); scratchpad (18 bases); GTE via PsyCross |
| `D_8004B528` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004B590` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004B610` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004BC60` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004BCC8` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004BD48` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `ColSphere` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004C4EC` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004C550` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004C5D0` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004C620` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004C820` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004CFAC` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004D010` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004D090` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004D0E0` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004D2E0` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `ColFindFloor` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8004DF24` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `ColSpyroInHitbox` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `ColMoby` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004E5B8` | mips2c v0, unit `asm/collision.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8001F158` | mips2c v0, unit `asm/renderers/r_moby.s` (4699 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | 4 packed-pointer rebase(s); scratchpad (6 bases); GTE via PsyCross |
| `func_8001F798` | mips2c v0, unit `asm/renderers/r_moby.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_800208FC` | mips2c v0, unit `asm/renderers/r_moby.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80020F34` | mips2c v0, unit `asm/renderers/r_moby.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_80022110` | mips2c v0, unit `asm/renderers/r_moby.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_80022148` | mips2c v0, unit `asm/renderers/r_moby.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_80022180` | mips2c v0, unit `asm/renderers/r_moby.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80022A2C` | mips2c v0, unit `asm/renderers/r_moby.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_800258F0` | mips2c v0, unit `asm/renderers/r_environment.s` (6199 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | scratchpad (16 bases); GTE via PsyCross |
| `func_8002A6FC` | mips2c v0, unit `asm/renderers/r_environment.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `EnvAnimSetFlags` | mips2c v0, unit `asm/renderers/r_environment.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `EnvAnimGetState` | mips2c v0, unit `asm/renderers/r_environment.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `EnvAnimSetFrame` | mips2c v0, unit `asm/renderers/r_environment.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8002B4AC` | mips2c v0, unit `asm/renderers/r_environment.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8004EBA8` | mips2c v0, unit `asm/renderers/r_cyclorama.s` (2058 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | scratchpad (8 bases); GTE via PsyCross |
| `func_8004F000` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8004F4BC` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8004F7E8` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_8004F890` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8004FDAC` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8004FEA0` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8004FF90` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_80050028` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80050240` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8005060C` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_800506B4` | mips2c v0, unit `asm/renderers/r_cyclorama.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80023AC4` | mips2c v0, unit `asm/renderers/r_pete.s` (1931 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | 6 packed-pointer rebase(s); scratchpad (4 bases); GTE via PsyCross |
| `D_80024054` | mips2c v0, unit `asm/renderers/r_pete.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_800240F8` | mips2c v0, unit `asm/renderers/r_pete.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `D_800241A8` | mips2c v0, unit `asm/renderers/r_pete.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_800573C8` | mips2c v0, unit `asm/renderers/r_particles.s` (1540 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | scratchpad (4 bases); GTE via PsyCross |
| `func_800580F4` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_800584C4` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_8005882C` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80058864` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80058AE8` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80058B60` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80058B68` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80058BA8` | mips2c v0, unit `asm/renderers/r_particles.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80059A48` | mips2c v0, unit `asm/renderers/r_shadows.s` (650 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | 3 packed-pointer rebase(s); scratchpad (12 bases); GTE via PsyCross |
| `func_80059F8C` | mips2c v0, unit `asm/renderers/r_shadows.s` | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | see first row of unit |
| `func_80058D64` | mips2c v0, unit `asm/renderers/r_flame.s` (452 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). | GTE via PsyCross |
| `func_80033F08` | mips2c v0, unit `asm/nonmatchings/camera/func_80033F08.s` (164 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80034480` | mips2c v0, unit `asm/nonmatchings/camera/func_80034480.s` (513 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80034CE8` | mips2c v0, unit `asm/nonmatchings/camera/func_80034CE8.s` (687 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_800357A4` | mips2c v0, unit `asm/nonmatchings/camera/func_800357A4.s` (493 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80035FB4` | mips2c v0, unit `asm/nonmatchings/camera/func_80035FB4.s` (1302 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80050BD0` | mips2c v0, unit `asm/nonmatchings/cyclorama/func_80050BD0.s` (1287 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_8002F3E4` | mips2c v0, unit `asm/nonmatchings/gamestates/update/func_8002F3E4.s` (2108 insns) | Not reached in run1. Builds and links. |  |
| `func_800314B4` | mips2c v0, unit `asm/nonmatchings/gamestates/update/func_800314B4.s` (1041 insns) | Not reached in run1. Builds and links. |  |
| `func_8003BFC0` | mips2c v0, unit `asm/nonmatchings/moby_helpers/func_8003BFC0.s` (230 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_credits_8007AA50` | mips2c v0, unit `asm/nonmatchings/overlays/credits/func_credits_8007AA50.s` (1381 insns) | Not reached in run1. Builds and links. |  |
| `func_credits_8007BFD0` | mips2c v0, unit `asm/nonmatchings/overlays/credits/func_credits_8007BFD0.s` (218 insns) | Not reached in run1. Builds and links. |  |
| `func_8003EA68` | mips2c v0, unit `asm/nonmatchings/pete/func_8003EA68.s` (1324 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_8003FE40` | mips2c v0, unit `asm/nonmatchings/pete/func_8003FE40.s` (1098 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80041670` | mips2c v0, unit `asm/nonmatchings/pete/func_80041670.s` (2699 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80043FE4` | mips2c v0, unit `asm/nonmatchings/pete/func_80043FE4.s` (3865 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80047B60` | mips2c v0, unit `asm/nonmatchings/pete/func_80047B60.s` (889 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80048D10` | mips2c v0, unit `asm/nonmatchings/pete/func_80048D10.s` (562 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
| `func_80049FAC` | mips2c v0, unit `asm/nonmatchings/pete/func_80049FAC.s` (149 insns) | No differential test yet. Ran without fault in run1 (title, menu, two attract demos). |  |
