/* The presenter's own copies of a few game calls (stage 7g, decisions
 * D128), compiled in the presenter build (cmake/presenter.cmake, with
 * pres_names.h), so the overlay functions named below are the presenter's
 * copies.
 *  - pres_flight5, pres_flight4, pres_balloon4: the draws of the flight
 *    results (GS_FlightResults) and of the balloonist's talk (GS_Balloonist)
 *    call an overlay's function through a pointer the level set
 *    (D_8007567C, D_800758C4, D_800758D8; overlay_pointers.c). pres_names.h
 *    turns each such call into one through the pointer returned here: the
 *    presenter's copy of the same level's function, which pres_draw.c finds
 *    on the game thread (g_pres_ovl), or a function that does nothing if
 *    the game's pointer is none of them (counted there);
 *  - pres_letters (D144): the same for the letter count PAL's flight
 *    results call through g_PalLetterCount;
 *  - pres_rand: libc's rand (rand_psyq.c) on the snapshot's seed
 *    (D_80075AC0, pres_snap_names.h);
 *  - pres_SpuSetVoiceAttr: the SPU write PlaySound makes, dropped, as D30's
 *    pass dropped it (libspu.c's g_psx_spu_frozen, until D129). */
#include <stddef.h>

#include "pres_draw.h"

#define FLIGHT(n) void Flight5_LEVEL_##n(void); int Flight4_LEVEL_##n(int, void *, int); \
  int func_pal_80081758_LEVEL_##n(char *);
#define BALLOON(n) void Balloonist4_LEVEL_##n(void);
PRES_FLIGHT_LEVELS(FLIGHT)
PRES_BALLOON_LEVELS(BALLOON)

static void none(void) {}
static int none4(int a, void *b, int c) { return (void)a, (void)b, (void)c, 0; }
static int none_letters(char *t) { return (void)t, 0; }

#define F5(n) Flight5_LEVEL_##n,
#define F4(n) Flight4_LEVEL_##n,
#define B4(n) Balloonist4_LEVEL_##n,
#define FL(n) func_pal_80081758_LEVEL_##n,
static void (*const s_flight5[])(void) = {PRES_FLIGHT_LEVELS(F5) none};
static int (*const s_flight4[])(int, void *, int) = {PRES_FLIGHT_LEVELS(F4) none4};
static void (*const s_balloon4[])(void) = {PRES_BALLOON_LEVELS(B4) none};
static int (*const s_letters[])(char *) = {PRES_FLIGHT_LEVELS(FL) none_letters};

static int pick(int which, int n) {
  const int i = g_pres_ovl[which];
  return i >= 0 && i < n ? i : n;
}

void (**pres_flight5(void))(void) {
  return (void (**)(void))&s_flight5[pick(PRES_OVL_FLIGHT5, PRES_FLIGHTS)];
}

int (**pres_flight4(void))(int, void *, int) {
  return (int (**)(int, void *, int))&s_flight4[pick(PRES_OVL_FLIGHT4, PRES_FLIGHTS)];
}

void (**pres_balloon4(void))(void) {
  return (void (**)(void))&s_balloon4[pick(PRES_OVL_BALLOON4, PRES_BALLOONS)];
}

int (**pres_letters(void))(char *) {
  return (int (**)(char *))&s_letters[pick(PRES_OVL_LETTERS, PRES_FLIGHTS)];
}

extern unsigned int D_80075AC0;

int pres_rand(void) {
  D_80075AC0 = D_80075AC0 * 0x41C64E6Du + 0x3039u;
  return (int)((D_80075AC0 >> 16) & 0x7FFFu);
}

void pres_SpuSetVoiceAttr(void *attr) { (void)attr; }
