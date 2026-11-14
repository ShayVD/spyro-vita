# tiny-AES-c (vendored)

`aes.c`, `aes.h` and `unlicense.txt` from https://github.com/kokke/tiny-AES-c (kokke's upstream
repository) at commit `23856752fbd139da0b8ca6e471a13d5bcc99a08d` (2024-10-04), unchanged; the
Unlicense. Used for the PS1 Classics EBOOT's PGD header (`src/platform/common/pbp.c`, through
`psx_aes128.h` and `aes128.c`; decisions D157): AES-128, one block at a time. `aes.c` is built
with `ECB=1`, `CBC=0`, `CTR=0` (`cmake/platform_common.cmake`); `aes128.c` is the only file that
includes `aes.h`, with the same three switches. `tests/pbp` checks it against FIPS 197.

Update by copying the three files again and changing the commit here and in
`THIRD_PARTY_NOTICES.md`.
