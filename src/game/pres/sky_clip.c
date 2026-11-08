/* The sky seen through portals (sky_clip_hand.c) in the presenter's build (stage 7e,
 * D124; cmake/presenter.cmake): its entries take the presenter's plain
 * form, the hand C at fast_render=2 with no unit or check to pick, as the
 * native build's are (render_hand.h HAND_UNIT_FN_); on 7d2's readable code
 * (RENDER_CTX_READABLE) its own presenter entries. pres_names.h comes first
 * (-include). */
#if RENDER_CTX_READABLE
/* 7d2's readable code (render_hand.h HAND_UNIT_FN_RC: NAME_pres) */
#define NGTE_RC 1
/* the entries are named here by the hand C's own macro (NAME_pres), so
 * pres_names.h's mapping of the callers' names stays out of this file */
#undef func_8001F158
#undef func_800208FC
#undef func_8004F000
#undef func_8004F4BC
#undef func_80050240
#undef func_8004FEA0
#include "sky_clip_hand.c"
#else
#include "render_hand.h"
#undef HAND_UNIT_FN_
#define HAND_UNIT_FN_(name, hand, check)                                       \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#include "sky_clip_hand.c"
#endif
