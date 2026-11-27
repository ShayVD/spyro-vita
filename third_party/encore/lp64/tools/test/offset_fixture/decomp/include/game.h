/* A stand-in decomp's header for the offset model's tools (no game's code): slots spelled P32F, as p32_patch.py
 * leaves them, and macros of the kinds a decomp has. */
#ifndef GAME_H
#define GAME_H
typedef struct Moby Moby;
typedef P32F(Moby *) MobySlot;
struct Moby {
  short m_Type;
  short m_Flags;
  P32F(Moby *) m_Next;
  P32F(unsigned char *) m_Data;
  P32F(MobySlot *) m_List;
  int m_Pos[3];
};
extern MobySlot g_Head;
extern MobySlot g_Table[4];
extern P32F(Moby *) g_Spare;
/* a whole macro whose expansion is a slot: wrapped where it is used */
#define FIRST_MOBY g_Head
/* a strict part of a #define's body: rewritten in the body, once */
#define NEXT_TYPE(m) ((m)->m_Next->m_Type)
/* a whole macro made by a token paste: wrapped where it is used */
#define SLOT_OF(n) g_##n
/* a slot read inside a token paste's expression: listed, done by hand */
#define SPARE_FLAGS(n) (g_##n->m_Flags)
#endif
