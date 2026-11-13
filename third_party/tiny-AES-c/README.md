# tiny-AES-c (vendored)

`aes.c`, `aes.h` and `unlicense.txt` from https://github.com/mistial-dev/tiny-AES-c at commit
`4258d2aebf19d581571dc80e609b7b3f256ccdf4` (kokke's tiny-AES-c, the owner's fork; the Unlicense).
Used for the PS1 Classics EBOOT's PGD header (`src/platform/common/pbp.c`, through
`psx_aes128.h`; decisions D157). Built with `AES128`, ECB only, the fast S-box
(`cmake/platform_common.cmake`). Update by copying the three files again and changing the commit
here.
