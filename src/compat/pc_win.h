/* The PC build for Windows (docs/pc-build.md, D241), from p32.h in every C
  * unit there: the BSD/SysV type names glibc's <sys/types.h> gives the
 * decomp, PsyCross and our C on Linux, which mingw's doesn't. Windows' long
 * is 32 bits (LLP64), as PsyQ's. */
#ifndef SPYRO_PC_WIN_H
#define SPYRO_PC_WIN_H
#if defined(_WIN32) && !defined(__ASSEMBLER__)
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
#endif
#endif
