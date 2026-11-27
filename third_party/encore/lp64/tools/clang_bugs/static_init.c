/* clang bug 3: a __ptr32 static initializer holding an address crashes the backend ("Unsupported expression in static
 * initializer: addrspacecast"). The workaround (-DWORKAROUND): lp64_p32.h's P32_INIT, a normal table copied into the
 * P32 one by a constructor.
 *   clang --target=x86_64-linux-gnu -fms-extensions -O1 -c static_init.c
 * From spyro-vita tools/ps4/clang_bugs/static_init.c @ c25a3ea (MIT; D174). */
#include "lp64_p32.h"
#ifndef WORKAROUND
char *P32 g_Names[3] = {"A", "BB", "CCC"};
#else
char *P32_INIT(g_Names)[3] = {"A", "BB", "CCC"};
P32_INIT_ARRAY(char, g_Names)
#endif
