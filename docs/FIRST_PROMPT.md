We're starting the Spyro the Dragon → PS Vita port. Read CLAUDE.md and docs/research.md first.

This session is milestone M0. It's investigation and planning only: no port code yet.

1. **Check the environment.**
   - The submodules are checked out.
   - The decomp's matching build in Docker produces a PSX.EXE whose SHA-1 equals `84E3728AB94720D0873E2514ADF4AADE4935E0C5`.
   - `disc/` contains the extracted SCUS-94228 files.
   - The VitaSDK, vitaGL and SDL2 packages are installed.

   Report each check as pass or fail. Fix only environment problems.

2. **Map the decomp** (`third_party/spyro-1`) and produce `docs/porting-plan.md` with:
   - **Remaining non-C code:** every function in `asm/nonmatchings/`, plus everything in `asm/` that was originally hand-written assembly. Give each one's size, what it does (as far as can be told) and who calls it. Rank them by how hard they'll be to translate.
   - **PsyQ surface:** every PsyQ library call and GTE macro or inline assembly the game uses, checked against what PsyCross implements. Mark each as covered, partial or missing.
   - **Overlays:** how they're built and loaded, and a concrete design for linking them statically and dispatching by level on the Vita (no JIT).
   - **Pointer and data risks:** places where data loaded from WAD.WAD holds raw PS1 addresses or gets relocated at run time, and anything that assumes the PS1 memory map (0x80000000, scratchpad at 0x1F800000). Explain how a 32-bit host handles each one.
   - **Rendering path:** how Spyro builds its ordering tables and primitives, including the cyclorama skybox and the LOD/fade. List what PsyCross plus vitaGL covers and what needs custom work.
   - **Audio and CD:** the SPU, XA music streaming and CD reads, mapped onto PsyCross and SDL2 file I/O.

3. **Recommend how to translate the unmatched functions.** Options:
   - (a) hand-decompile to non-matching C
   - (b) write our own MIPS→C translator in `tools/`
   - (c) use techniques from `../psxrecomp-ref` without copying its code
   - (d) a mix of these

   Weigh it against the size of `pete/func_80043FE4` and the other large ones. Propose how we'll check that each translated function behaves the same as the original, for example by recording and comparing traces against an emulator such as DuckStation.

4. **Lift build patterns** from `~/Projects/kh-bbs-vita` (CMake, vitaGL init, VPK packaging, runtime/host split) and `~/Projects/kh1-decomp` (host shims, data relocations, overlays). Write down what we reuse in `docs/decisions.md`.

5. **End with** a task breakdown for M1 (host build boots to the title screen) that's concrete enough to start next session, and a list of open questions for me.

Don't modify the submodules. Don't commit anything from `disc/`.
