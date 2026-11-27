/* A synthetic decomp header for lp64/tools/test (no game's code): the shapes a 4-byte-pointer decomp's headers have.
 * Written for the kit, after the cases spyro-vita's patch met (spyro-vita tools/ps4/p32_patch.py @ c25a3ea, D174). */
#ifndef GAME_H
#define GAME_H

typedef unsigned int u_int;
typedef unsigned long u_long; /* the SDK's: 4 bytes on the console, 8 on LP64 */

typedef struct Moby Moby;
struct Moby {
  short m_Type;
  Moby *m_Next;
  int **m_Table;
  unsigned char *m_Bytes[2];
  u_long m_Flags;
};

extern Moby *g_Player;
extern Moby *g_List[4];
extern char *g_Names[3];
extern void (*g_Update)(Moby *); /* a function pointer label: the port's own 8-byte global on LP64 */
/* in a comment, untouched: Moby *g_Fake; */

int walk(void);
int names(void);
void store(void *p);
int first_set(void *p);
int addr(Moby *m);

#endif
