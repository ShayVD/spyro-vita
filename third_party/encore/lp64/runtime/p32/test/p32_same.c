/* lp64_p32 on a 32-bit target compiles to nothing: this unit, built once through the header and once with
 * LP64_TEST_PLAIN (the same text with every P32 spelled as nothing, by hand), must give the same assembly
 * (test/p32_same_test.sh). Covers P32 on fields, labels and `T *P32 *`, and the P32_INIT forms, whose constructors
 * must not exist here; and the offset model's spelling (P32F slots, every accessor, P32_ENC/P32_DEC/P32_ARG), which
 * must also be the code's own text here (spyro-vita D249, D258, D262: the Vita's binary unchanged).
 * From spyro-vita src/compat/p32.h @ c25a3ea (MIT): the shapes its patch writes (D174), on synthetic data. */
#ifdef LP64_TEST_PLAIN
#define P32
#define P32F(T) T
#define P32_INIT(name) name
#define P32_INIT_ARRAY(type, name)
#define P32_INIT_SCALAR(type, name)
#else
#include "lp64_p32.h"
#if LP64_P32_ON
#error "p32_same is for targets where P32 is empty"
#endif
#endif

typedef struct Thing Thing;
struct Thing {
  short m_Type;
  Thing *P32 m_Next;
  int *P32 m_Data[2];
  Thing *P32 *P32 m_List;
};

extern char *P32 g_Names[3]; /* as the game's header declares them */
extern Thing *P32 g_Second;
Thing g_Things[4];
Thing *P32 g_First;
Thing *P32 g_Table[4] = {0};
char *P32_INIT(g_Names)[3] = {"A", "BB", "CCC"};
Thing *P32_INIT(g_Second) = &g_Things[1];

int sum(Thing *t) {
  Thing *P32 *w;
  int n = 0;
  for (w = t->m_List; *w; w++)
    n += (*w)->m_Type + *(*w)->m_Data[1];
  return n + g_First->m_Type + g_Names[2][1] + g_Second->m_Type;
}

/* the offset model's spelling: slots and accessors, against the plain code they replace */
typedef struct Node Node;
struct Node {
  P32F(Node *) m_Next;
  P32F(unsigned char *) m_Bytes;
  P32F(P32F(Node *) *) m_List;
  int m_Val;
};
P32F(Node *) g_Head;
unsigned g_Addr;

/* (the casts below are 32-bit code: a 64-bit compiler only checks that the header compiles, run_host_tests.sh's p32_wide) */
#if __SIZEOF_POINTER__ == 4
#ifdef LP64_TEST_PLAIN
int walk(Node *n, unsigned char *file, unsigned i) {
  int s = 0;
  Node *p;
  for (p = g_Head; p; p = p->m_Next)
    s += p->m_Val;
  n->m_Next = g_Head;
  n->m_Bytes += 2;
  n->m_Bytes -= 1;
  s += *n->m_Bytes++;
  s += *++n->m_Bytes;
  s += *n->m_Bytes--;
  s += *--n->m_Bytes;
  n->m_Bytes = (__typeof__(n->m_Bytes))((unsigned int)(file) + (unsigned int)(n->m_Bytes));
  g_Addr = (unsigned)n->m_List;
  s += ((Node *)i)->m_Val;
  return s + (*n->m_List)->m_Val;
}
#else
int walk(Node *n, unsigned char *file, unsigned i) {
  int s = 0;
  Node *p;
  for (p = P32_GET(g_Head); p; p = P32_GET(p->m_Next))
    s += p->m_Val;
  P32_SET(n->m_Next, P32_GET(g_Head));
  P32_ADD(n->m_Bytes, 2);
  P32_SUB(n->m_Bytes, 1);
  s += *P32_POSTINC(n->m_Bytes);
  s += *P32_PREINC(n->m_Bytes);
  s += *P32_POSTDEC(n->m_Bytes);
  s += *P32_PREDEC(n->m_Bytes);
  P32_PATCH(n->m_Bytes, file);
  g_Addr = (unsigned)P32_ENC(P32_GET(n->m_List));
  s += ((Node *)P32_DEC(i))->m_Val;
  return s + P32_GET(*P32_GET(n->m_List))->m_Val;
}
#endif
#endif

P32_INIT_ARRAY(char, g_Names)
P32_INIT_SCALAR(Thing, g_Second)
