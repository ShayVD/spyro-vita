/* The offset model (lp64_p32.h with LP64_P32_OFFSET, lp64_p32_image.c) on an AArch64 host: macOS (a PIE slid above
 * __PAGEZERO, the base its Mach-O header) or Linux (non-PIE at 0x2_0123_0000, the base __executable_start; static, or
 * dynamic with lp64_low_image_dyn.c's reservation). A synthetic game struct keeps its 32-bit layout with P32F slots;
 * each accessor stores an offset and reads a pointer; the casts' encode and decode, a sentinel kept, a loader's fix-up,
 * the P32_INIT tables, the data image's words, the image's checks, and a pointer cut to 32 bits that crashes.
 *   p32_offset_test            the checks; exit 0
 *   p32_offset_test check      (built with LP64_P32_CHECK=1) stores a host pointer: must abort with the file and line
 * LP64_TEST_DYN=1: lp64_low_image_dyn.c is linked in, and its reservation must have taken pages.
 * Mutants (must fail): LP64_P32_MUTANT=cut (an encode that keeps the low 32 bits, the miss D262 and D265 were about),
 * LP64_P32_MUTANT=words (the data image's words left unstored).
 * Written for the kit, after spyro-vita's linuxa64 and macOS hosts (D262, D265) @ 36d9e07, on synthetic data. */
#include "lp64_p32_image.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#if !LP64_P32_OFFSET
#error "p32_offset_test is the offset model's test: build it for AArch64 (or with -DLP64_P32_OFFSET=1)"
#endif

#if defined(LP64_P32_MUTANT_CUT)
#undef lp64_p32_enc_
#define lp64_p32_enc_(p) ((unsigned)(__UINTPTR_TYPE__)(p))
#endif

typedef struct Thing Thing;
/* two P32F spellings are two distinct unions: slots that meet share a typedef (lp64_p32.h) */
typedef P32F(Thing *) ThingSlot;
struct Thing {
  short m_Type;
  short m_Flags;
  P32F(Thing *) m_Next;
  P32F(int *) m_Data;
  P32F(unsigned char *) m_Bytes[3];
  P32F(ThingSlot *) m_List;
  int m_Pos[3];
};
_Static_assert(sizeof(Thing) == 40, "Thing keeps its 32-bit size");
_Static_assert(__builtin_offsetof(Thing, m_Data) == 8, "m_Data at its 32-bit offset");
_Static_assert(__builtin_offsetof(Thing, m_List) == 24, "m_List at its 32-bit offset");
_Static_assert(sizeof(P32F(Thing *)) == 4 && _Alignof(P32F(Thing *)) == 4, "a slot is 4 bytes, 4-aligned");

static Thing s_things[4];        /* the game's memory: .bss, in the image */
static char s_file[64];          /* a "file" a loader read, with an offset to fix up */
P32F(Thing *) g_First;           /* a data label */
ThingSlot g_Table[4];
unsigned g_Word;                 /* a pointer word of the data image (an assembler's `.long D_x`) */
static const char s_text[] = "lp64";

/* address-initialised tables (lp64_p32.h P32_INIT): declared as a header would, filled at start-up */
extern P32F(char *) g_Names[3];
extern P32F(Thing *) g_Second;
char *P32_INIT(g_Names)[3] = {"A", "BB", "CCC"};
Thing *P32_INIT(g_Second) = &s_things[1];

/* the data image's words: {slot, target}, ending {0, 0} */
static const uint64_t s_words[] = {(uint64_t)(uintptr_t)&g_Word, (uint64_t)(uintptr_t)&s_things[3], 0, 0};

#if LP64_TEST_DYN
int lp64_reserved_pages(void); /* lp64_low_image_dyn.c, linked in (Linux's dynamic variant) */
#endif

/* an entry that takes the caller's pointers as 32-bit words (P32_ARG_T) */
static unsigned entry(P32_ARG_T a) { return P32_ARG(a); }

static int s_fails;
#define CHECK(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      printf("p32_offset_test: FAILED %s (line %d)\n", #c, __LINE__);                                                  \
      s_fails++;                                                                                                       \
    }                                                                                                                  \
  } while (0)

static uint32_t raw(const void *slot) {
  uint32_t r;
  memcpy(&r, slot, 4);
  return r;
}
static uint32_t ofs(const void *p) { return (uint32_t)((uintptr_t)p - (uintptr_t)lp64_p32_base); }

/* reading p in a child: 1 if the child died of SIGSEGV or SIGBUS */
static int faults(const volatile int *p) {
  pid_t c = fork();
  int st = 0;
  if (c == 0) {
    volatile int v = *p;
    (void)v;
    _exit(0);
  }
  waitpid(c, &st, 0);
  return WIFSIGNALED(st) && (WTERMSIG(st) == SIGSEGV || WTERMSIG(st) == SIGBUS);
}

/* host memory far from the image: an unhinted mmap may land right after it, inside the window (macOS does), where an
 * offset reaches it and the check can't call it the host's */
static void *host_page(void) {
  void *p = mmap((char *)lp64_p32_base + (64ull << 30), 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return p == MAP_FAILED ? 0 : p;
}

int main(int argc, char **argv) {
  char why[200];
  Thing *t = &s_things[0];
  ThingSlot *walk;
  int i, n, words;
  void *host = host_page();

  if (argc > 1 && !strcmp(argv[1], "check")) {
    P32_SET(t->m_Next, (Thing *)host); /* a host pointer stored as a game address: the checking build stops here */
    printf("p32_offset_test: the checking build did not stop\n");
    return 1;
  }

  words = lp64_p32_image_start(s_words, why, sizeof why);
#if defined(LP64_P32_MUTANT_WORDS)
  g_Word = 0;
#endif
  printf("p32_offset_test: base %p, image end %p, host page %p, %d data word(s)\n", (void *)lp64_p32_base,
         (void *)lp64_p32_image_end(), host, words);
  CHECK(words == 1);
#if LP64_TEST_DYN
  printf("p32_offset_test: %d pages below 4 GB reserved\n", lp64_reserved_pages());
  CHECK(lp64_reserved_pages() > 0);
#endif
  CHECK((uintptr_t)lp64_p32_base >= 0x100000000ull);
  /* a base 4 GB-aligned hides a missed encode (a pointer cut to 32 bits *is* its offset): spyro-vita D262 */
  CHECK(((uintptr_t)lp64_p32_base & 0xffffffffull) != 0);
  CHECK(host && lp64_p32_within(&s_things[3], sizeof(Thing)) && lp64_p32_within(s_text, 5) && !lp64_p32_within(host, 1));
  CHECK(g_Word == ofs(&s_things[3]));
  CHECK(P32_DEC(g_Word) == (void *)&s_things[3]);

  /* a pointer written into a slot is stored as its offset and read back as itself; NULL is 0 */
  CHECK(P32_SET(t->m_Next, &s_things[2]) == &s_things[2]);
  CHECK(P32_GET(t->m_Next) == &s_things[2] && raw((char *)t + 4) == ofs(&s_things[2]));
  P32_SET(t->m_Data, (int *)0);
  CHECK(raw((char *)t + 8) == 0 && P32_GET(t->m_Data) == 0);
  P32_SET(t->m_Bytes[1], (unsigned char *)s_text);
  CHECK(P32_GET(t->m_Bytes[1])[3] == '4' && raw((char *)t + 16) == ofs(s_text));
  P32_SET(g_First, &s_things[1]);
  CHECK(P32_GET(g_First)->m_Type == 0 && raw(&g_First) == ofs(&s_things[1]));

  /* the compound forms step by the pointed-to type and give C's values */
  P32_SET(t->m_Next, &s_things[0]);
  CHECK(P32_ADD(t->m_Next, 2) == &s_things[2] && P32_GET(t->m_Next) == &s_things[2]);
  CHECK(P32_SUB(t->m_Next, 1) == &s_things[1]);
  CHECK(P32_POSTINC(t->m_Next) == &s_things[1] && P32_GET(t->m_Next) == &s_things[2]);
  CHECK(P32_PREINC(t->m_Next) == &s_things[3] && P32_PREDEC(t->m_Next) == &s_things[2]);
  CHECK(P32_POSTDEC(t->m_Next) == &s_things[2] && P32_GET(t->m_Next) == &s_things[1]);

  /* a P32F(P32F(T *) *) (ThingSlot) walks a table of slots 4 bytes a step */
  for (i = 0; i < 4; i++) {
    P32_SET(g_Table[i], &s_things[3 - i]);
    s_things[i].m_Type = (short)(10 + i);
  }
  P32_SET(t->m_List, g_Table);
  n = 0;
  for (walk = P32_GET(t->m_List); walk < g_Table + 4; walk++)
    n = n * 100 + P32_GET(*walk)->m_Type;
  CHECK(n == 13121110);

  /* the casts: a pointer as a 32-bit integer is its offset, and back; NULL, small numbers and sentinels kept */
  CHECK((unsigned)P32_ENC(&s_things[2]) == ofs(&s_things[2]));
  CHECK((Thing *)P32_DEC((unsigned)P32_ENC(&s_things[2])) == &s_things[2]);
  CHECK((unsigned)P32_ENC((void *)0) == 0 && P32_DEC(0u) == 0);
  CHECK((unsigned)P32_ENC((void *)(uintptr_t)0xffffffffu) == 0xffffffffu);
  CHECK((unsigned)P32_ENC(1234) == 1234);                     /* an integer passes through */
  CHECK(P32_DEC(&s_things[1]) == (void *)&s_things[1]);       /* a pointer passes through */
  CHECK(entry((P32_ARG_T)&s_things[1]) == ofs(&s_things[1])); /* a pointer argument encoded on entry */
  CHECK(entry(77) == 77);                                     /* an integer argument kept */

  /* a loader's fix-up: the slot holds an offset into the file just read */
  memcpy(s_file + 8, "fixup", 6);
  t->m_Bytes[2].p32_off = 8; /* as the disc's data has it */
  P32_PATCH(t->m_Bytes[2], s_file);
  CHECK(P32_GET(t->m_Bytes[2]) == (unsigned char *)s_file + 8 && !memcmp(P32_GET(t->m_Bytes[2]), "fixup", 6));

  /* the address-initialised tables, filled by constructors */
  CHECK(!strcmp(P32_GET(g_Names[2]), "CCC") && raw(&g_Names[0]) != 0 && P32_GET(g_Second) == &s_things[1]);

  /* a pointer cut to 32 bits (an integer cast instead of P32_ENC) points into the empty low 4 GB: it crashes */
  CHECK(faults((const volatile int *)(uintptr_t)(uint32_t)(uintptr_t)&s_things[2]));
  CHECK(!faults((const volatile int *)&s_things[2]));

  /* the image check refuses a data word outside the image */
  {
    static uint64_t bad[] = {0, 0, 0, 0};
    bad[0] = (uint64_t)(uintptr_t)&g_Word;
    bad[1] = (uint64_t)(uintptr_t)host;
    CHECK(lp64_p32_image_start(bad, why, sizeof why) == -1 && strstr(why, "outside the image"));
  }

  if (s_fails) {
    printf("p32_offset_test: %d failed\n", s_fails);
    return 1;
  }
  printf("p32_offset_test: ok\n");
  return 0;
}

P32_INIT_ARRAY(char, g_Names)
P32_INIT_SCALAR(Thing, g_Second)
