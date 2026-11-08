/* The sky seen through portals (sky_clip_hand.c) in the presenter's build (stage 7e,
 * D124; cmake/presenter.cmake): its entries take the presenter's plain
 * form, the hand C at fast_render=2 with no unit or check to pick, as the
 * native build's are (render_hand.h HAND_UNIT_FN_). pres_names.h comes
 * first (-include). */
#include "render_hand.h"
#undef HAND_UNIT_FN_
#define HAND_UNIT_FN_(name, hand, check)                                       \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#include "sky_clip_hand.c"
