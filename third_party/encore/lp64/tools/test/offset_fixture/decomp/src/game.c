/* A stand-in decomp's unit for the offset model's tools (no game's code). */
#include "game.h"

MobySlot g_Head;
MobySlot g_Table[4];
__typeof__(g_Spare) g_Spare; /* two P32F spellings are two types: borrow the header's */

int walk(Moby *m) {
  int n = 0;
  Moby *p;
  for (p = g_Head; p; p = p->m_Next)
    n += p->m_Type;
  m->m_Next = g_Head;
  g_Head = m;
  m->m_Data += 4;
  m->m_Data -= 2;
  n += *m->m_Data++;
  n += *--m->m_Data;
  m->m_Next = 0;
  n += FIRST_MOBY->m_Type;
  n += NEXT_TYPE(g_Table[1]);
  n += SLOT_OF(Spare)->m_Flags;
  n += SPARE_FLAGS(Spare);
  m->m_List = g_Table;
  return n + (*m->m_List)->m_Type;
}

/* the casts a decomp has: a pointer kept in an int, an int used as a pointer */
int cast_out(Moby *m) { return (int)m + (int)g_Head; }
Moby *cast_in(int i) { return (Moby *)i; }
int cast_nested(Moby *m) { return (int)*m->m_List; }
