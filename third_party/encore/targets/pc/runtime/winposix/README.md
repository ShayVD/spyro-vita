# winposix: POSIX calls a port's own code needs on Windows

[`enc_winposix.c`](enc_winposix.c), linked into a Windows (mingw) build only:

- **every file binary**: a constructor sets `_fmode` to `_O_BINARY`, so `open()` and `fopen()` without `"b"` never turn
  CR LF into LF in a disc image's bytes (ports open files as on POSIX, where there is no text mode);
- **`pread`**: a positional `ReadFile`, with the descriptor's offset put back. Windows moves the file pointer even for
  a positional read on a synchronous handle (every CRT descriptor's); the test caught it under Wine.

`mkdir` takes no mode on Windows: `#include <direct.h>` and `#define mkdir(path, mode) _mkdir(path)` at the call
sites, after the system headers (a macro defined first would break mingw's own declaration).

**Test:** [`test/winposix_test.c`](test/winposix_test.c), built by `targets/pc/test/desktop_runtime_tests.sh`, run on
Windows (CI) or under Wine; its mutant (text mode, a seek-and-read `pread`) must fail.

From spyro-vita `src/platform/pc/low_win.c` (its D241), with the offset fix. MIT.
