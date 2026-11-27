/* A unit that declares a slot as a plain pointer: links, and disagrees at run time (p32_decl_check.py finds it).
 * Only in the test's cross-unit run. */
typedef struct Moby Moby;
extern Moby *g_Spare;
Moby *spare(void) { return g_Spare; }
