# vita/cmake

`VpkApp.cmake`: `vpk_add_app()` packs a linked executable target as a VPK, after checking it still defines the two
process parameters (`vpk_check_procparams.cmake`). It runs the same commands as `../tools/vpk_pack.py`, the helper for
games built by make or scripts, so a game can move between the two without its VPK changing.

```cmake
include(${CMAKE_SOURCE_DIR}/third_party/vita-port-kit/vita/cmake/VpkApp.cmake)   # after vitasdk's toolchain file
vpk_add_app(my_game VPK "${CMAKE_BINARY_DIR}/my-game.vpk"
  TITLE_ID ABCD00001 VERSION 01.00 NAME "My Game"
  FILE "${art}/icon0.png" sce_sys/icon0.png
       "${art}/bg.png" sce_sys/livearea/contents/bg.png
       "${art}/startup.png" sce_sys/livearea/contents/startup.png
       "${CMAKE_CURRENT_SOURCE_DIR}/template.xml" sce_sys/livearea/contents/template.xml
  DEPENDS my_game_art         # targets that make FILE sources
  SCE_HEADROOM 0x4000)        # optional: room after the text segment (below)
```

The target is `my_game-vpk` (in `ALL`); its work files (`.velf`, `eboot.bin`, `param.sfo`, the check's stamp) are in
`my-game.vpk.d/`. Steps:

1. `arm-vita-eabi-nm` must show `sceUserMainThreadStackSize` and `_newlib_heap_size_user` with an address. A line
   without one is not enough: when the heap size is missing newlib still leaves an undefined weak reference
   (`         w _newlib_heap_size_user`), which a check of the names alone (kh1-decomp's before the kit) passes.
2. `vita-elf-create`, 3. `vita-make-fself -s -c`,
4. `vita-mksfoex -s APP_VER=.. -s TITLE_ID=.. -d ATTRIBUTE2=12` (the extended memory budget), 5. `vita-pack-vpk`.

`SCE_HEADROOM <bytes>` links the target with `-Wl,--defsym=__sce_headroom=<bytes>` (without it, the variable
`VPK_SCE_HEADROOM` if set; else nothing). `vita-elf-create` puts its SCE data (module info, import tables: 1.1 KB for
the test's tiny app, 2.8 KB for spyro-vita) after the text segment, and the data segment starts at a 64 KiB
boundary. When the text ends closer to it than that, packing fails "Cannot allocate N bytes for SCE data at end of
segment 0; segment 1 overlaps", and any change that grows or shrinks the text can bring that on. vitasdk's linker
script adds `__sce_headroom` bytes before aligning the data segment, so `0x4000` keeps 16 KiB free; it costs address
space (at most one more 64 KiB step), not memory ([gotchas](../docs/gotchas.md) 17, spyro-vita D32). Off by default
so an existing game's layout doesn't move; a new game should set it. `vpk_pack.py` packs an ELF already linked, so
it can't add the room: it warns when the gap is under 16 KiB, names the fix when `vita-elf-create` fails that way,
and `vpk_pack.py check` prints the gap and the symbol.

Comparing two builds: `vita-elf-create` writes the ELF's file name into the eboot as the module name, so pack a
copy under the same file name, or the eboot.bin differs by those bytes alone. Everything else in the steps is
deterministic (the same ELF gives the same eboot.bin, param.sfo and files; only the zip's timestamps differ).

Not offered (no game here needs them yet): vitasdk's `vita_create_self` options (`UNSAFE`, `NOASLR`, `CONFIG` ...) and
other `param.sfo` keys. Add them as arguments here and in `vpk_pack.py` together.

**From:** kh1-decomp `tools/vita/build.py` @ `85befa4a` and spyro-vita `src/platform/vita/CMakeLists.txt` @ `987363e`
(`SCE_HEADROOM`: the same file @ `bb62f38`, D32).
**Test:** `../tools/test/vpk_pack_test.sh` (needs vitasdk, cmake, ninja): both helpers on a good app give the same
`param.sfo` values and file list, and both refuse an app with no process parameters and one whose LTO dropped them.
An app padded so its text ends 500 bytes before the data segment fails "segment 1 overlaps" in both (`vpk_pack.py`
warning first and naming the fix); linked with the headroom (by hand, `SCE_HEADROOM`, `VPK_SCE_HEADROOM`) it packs,
the flag is on CMake's link line and `arm-vita-eabi-nm` shows `00004000 A __sce_headroom`.
