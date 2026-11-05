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
  DEPENDS my_game_art)        # targets that make FILE sources
```

The target is `my_game-vpk` (in `ALL`); its work files (`.velf`, `eboot.bin`, `param.sfo`, the check's stamp) are in
`my-game.vpk.d/`. Steps:

1. `arm-vita-eabi-nm` must show `sceUserMainThreadStackSize` and `_newlib_heap_size_user` with an address. A line
   without one is not enough: when the heap size is missing newlib still leaves an undefined weak reference
   (`         w _newlib_heap_size_user`), which a check of the names alone (kh1-decomp's before the kit) passes.
2. `vita-elf-create`, 3. `vita-make-fself -s -c`,
4. `vita-mksfoex -s APP_VER=.. -s TITLE_ID=.. -d ATTRIBUTE2=12` (the extended memory budget), 5. `vita-pack-vpk`.

Comparing two builds: `vita-elf-create` writes the ELF's file name into the eboot as the module name, so pack a
copy under the same file name, or the eboot.bin differs by those bytes alone. Everything else in the steps is
deterministic (the same ELF gives the same eboot.bin, param.sfo and files; only the zip's timestamps differ).

Not offered (no game here needs them yet): vitasdk's `vita_create_self` options (`UNSAFE`, `NOASLR`, `CONFIG` ...) and
other `param.sfo` keys. Add them as arguments here and in `vpk_pack.py` together.

**From:** kh1-decomp `tools/vita/build.py` @ `85befa4a` and spyro-vita `src/platform/vita/CMakeLists.txt` @ `987363e`.
**Test:** `../tools/test/vpk_pack_test.sh` (needs vitasdk, cmake, ninja): both helpers on a good app give the same
`param.sfo` values and file list, and both refuse an app with no process parameters and one whose LTO dropped them.
