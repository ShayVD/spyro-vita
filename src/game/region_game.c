/* The release's game data the port keeps beside the US-shaped labels
 * (docs/regions-plan.md §2.3, §3.4; decisions D134).
 *
 * g_WadHeader and g_LevelHeader are PS1 RAM labels laid out as the US
 * executable's (D23, D106), so a PAL header can't be copied over them: PAL's
 * WAD header is 912 bytes (114 entries, 16 cutscene slots) where the US one
 * is 816, and PAL's level header 656 (5 x 6 dragon slots) where the US one is
 * 464. WadInitialize and the level-header copies come here instead
 * (include/wad.h WAD_HEADER_COPY, LEVEL_HEADER_COPY): the US header is copied
 * as before; a PAL one fills the US-shaped label with the same entries PAL's
 * code reads at those places, and its cutscene and dragon slots go to the
 * arrays below, which PAL's code paths read (WAD_CUTSCENE, LEVEL_DRAGON_PTR).
 * Every write is bounded by the label's own size. */
#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stddef.h>
#include <string.h>

#include "common.h"
#include "camera.h"
#include "cutscene.h"
#include "moby.h"
#include "wad.h"

#include "psx_region.h"
#include "region_data.h"

void PortFatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

_Static_assert(sizeof(g_WadHeader) == 816, "the port's WAD header is the US one");
_Static_assert(sizeof(LevelHeader) == 464, "the port's level header is the US one");

/* PAL only; US and JP keep them 0 and never read them (include/region.h) */
int g_Language;
int g_ScreenOffsetX = 4; /* PAL's start values (PAL 0x8007B74C, 0x8007B750) */
int g_ScreenOffsetY = 16;
int g_PalCardHeaderDirty; /* PAL 0x8007C148: the card header needs writing again (D139) */
int g_PalTitleCardLoad;   /* PAL 0x8007BFF0: the title's first read of the card header, 0-6, 7 done (D139) */
int (*g_PalLetterCount)(char *pText); /* PAL 0x8007C0B8: the flight overlay's letter counter, set by SetOverlayPointers (D145) */
OffsetLength g_WadCutscenesPal[16];

/* PAL's recorded cutscenes (the title's flyby, the intro and the endings'
 * camera and moby samples, the dragons' talk) are sampled for its 25 Hz
 * ticks: its flyby's loop points are US's x5/6 (976, 1120, 1274 against
 * 1172, 1344, 1530; titlescreen.c), and a dragon's voice delay is scaled
 * the same way (DragonVoiceDelay). So they count 50 Hz vblanks. At US
 * timing (PAL 60, the only way players run PAL since D150), d of our 60 Hz
 * vblanks are d * 5 / 6 of them, the remainder carried to the next call;
 * otherwise d. Without it PAL 60's intro, endings and dragons ran 20% fast
 * and ended before their voices (D138's open "PAL 60's cutscenes"). */
int g_PalCutsceneCarry; /* saved in states (savestate.c's REGN chunk) */
int PortCutsceneTicks(int d) {
  int n;
  if (g_Region != PSX_REGION_PAL || g_RegionTiming)
    return d;
  n = d * 5 + g_PalCutsceneCarry;
  g_PalCutsceneCarry = n % 6;
  return n / 6;
}

/* PAL at US timing's cutscene time in sixths of its 50 Hz vblanks (1/300 s):
 * the tick PortCutsceneTicks left and the remainder it carries. It moves by
 * exactly 5 a vblank of ours, 10 a 30 Hz tick, where the tick moves 2, 2, 1
 * (D151). -1 when PAL isn't at US timing. */
int PortCutsceneTime6(int ticks) {
  if (g_Region != PSX_REGION_PAL || g_RegionTiming)
    return -1;
  return ticks * 6 + g_PalCutsceneCarry;
}

/* An angle between two of a cutscene's samples, f twelfths of the way, the
 * short way round. A sample itself is kept as the data has it (0-4096). */
/* Whether two camera samples are a cut, not a move: a shot ends at the
 * first and the next starts at the second. Then the camera holds the first
 * until the second's time, as the original does. The presenter's jump
 * (present.c's JUMP, 0x2000 on an axis) for the position, and an eighth of
 * a turn for an angle. */
int PortCutsceneCut(const int *p0, const int *p1) {
  int i;
  for (i = 0; i < 3; i++)
    if (p1[i] - p0[i] > 0x2000 || p0[i] - p1[i] > 0x2000)
      return 1;
  for (i = 3; i < 6; i++) {
    const int d = ((p1[i] - p0[i] + 2048) & 4095) - 2048;
    if (d > 0x200 || d < -0x200)
      return 1;
  }
  return 0;
}

int PortCutsceneAngle(int a0, int a1, int f, int n) {
  int d = ((a1 - a0 + 2048) & 4095) - 2048;
  if (f == 0)
    return a0;
  return (a0 + d * f / n) & 4095;
}

/* func_8002BFE0 (cutscene.c) for PAL at US timing (D151): the intro's and
 * endings' camera and mobys at the cutscene's exact time instead of its
 * tick. The tick moves 2, 2, 1 of PAL's 50 Hz vblanks a 30 Hz tick (D150),
 * so a mobys' animation and path took a whole step on two ticks and half a
 * step on the third, and the camera, one sample per 2 of them, held for one
 * tick in six: the choppy characters the owner saw. At its exact time each
 * tick moves 5/6 of a 25 Hz sample. The mobys as func_8002BFE0 places them
 * (their frame and its 64ths, their position between two samples), from
 * the time; the camera between its two samples (the original shows a
 * sample a tick; 25 Hz samples have no 30 Hz one). Returns 0 when not PAL
 * at US timing or not the intro or an ending (the title's flyby keeps its
 * own tick), and func_8002BFE0 then runs as before. */
#define CUT_UNPACK(v, p)                                                       \
  ((v).x = ((p) & 0x3FF00000) >> 12, (v).y = ((p) & 0x000FFC00) >> 2,          \
   (v).z = ((p) & 0x000003FF) << 8)
int PortCutscenePlay(void) {
  const int t6 = PortCutsceneTime6(g_CutsceneLayout->m_CurrentTick);
  const CutsceneCameraData *c0, *c1;
  int i, k, f;
  if (t6 < 0 || g_Gamestate != GS_Cutscene)
    return 0;
  k = t6 / 12;
  f = t6 % 12;
  c0 = &g_CutsceneLayout->m_CameraData[k];
  c1 = k + 1 < g_CutsceneLayout->m_Duration ? c0 + 1 : c0;
  if (PortCutsceneCut(&c0->m_Position.x, &c1->m_Position.x))
    f = 0; /* a cut: hold the shot until the next one starts */
  g_Camera.m_Position.x = c0->m_Position.x + (c1->m_Position.x - c0->m_Position.x) * f / 12;
  g_Camera.m_Position.y = c0->m_Position.y + (c1->m_Position.y - c0->m_Position.y) * f / 12;
  g_Camera.m_Position.z = c0->m_Position.z + (c1->m_Position.z - c0->m_Position.z) * f / 12;
  g_Camera.m_Rotation.x = PortCutsceneAngle(c0->m_Rotation.x, c1->m_Rotation.x, f, 12);
  g_Camera.m_Rotation.y = PortCutsceneAngle(c0->m_Rotation.y, c1->m_Rotation.y, f, 12);
  g_Camera.m_Rotation.z = PortCutsceneAngle(c0->m_Rotation.z, c1->m_Rotation.z, f, 12);
  for (i = 0; i < g_CutsceneLayout->m_MobyCount; i++) {
    const int ppt = g_Models[1 + i]->m_Animations[0]->m_ProgressPerTick;
    /* the vblanks a frame lasts (func_8002BFE0's four cases), in sixths */
    const int span = 6 * (ppt == 16 ? 8 : ppt == 21 ? 6 : ppt == 32 ? 4 : 2);
    const int a = t6 / span, b = (t6 % span) * 64 / span;
    AnimationState *as = &g_LevelMobys[i].m_AnimationState;
    Vector3D v, v2;
    as->m_Animation = a / 50; /* PAL's data: 50 frames an animation (D135) */
    as->m_Frame = a % 50;
    as->m_NextAnimation = (a + 1) / 50;
    as->m_NextFrame = (a + 1) % 50;
    as->m_FrameProgress = b;
    CUT_UNPACK(v, g_CutsceneLayout->m_MobyData[i]->m_PackedPos[a]);
    CUT_UNPACK(v2, g_CutsceneLayout->m_MobyData[i]->m_PackedPos[a + 1]);
    g_LevelMobys[i].m_Position.x = (v.x * (64 - b) + v2.x * b) >> 6;
    g_LevelMobys[i].m_Position.y = (v.y * (64 - b) + v2.y * b) >> 6;
    g_LevelMobys[i].m_Position.z = (v.z * (64 - b) + v2.z * b) >> 6;
  }
  return 1;
}
OffsetLength g_LevelDragonsPal[5][6];

#define PAL_WAD_ENTRIES 114
#define PAL_LEVEL_HEADER 656

static OffsetLength entry(const unsigned char *raw, int i) {
  OffsetLength e;
  memcpy(&e, raw + i * 8, sizeof e);
  return e;
}

void PortWadHeader(void *raw_) {
  const unsigned char *raw = raw_;
  const PsxRegion *r = psx_region();
  __typeof__(g_WadHeader) h;
  int i;
  if (r->id != PSX_REGION_PAL) { /* US: the original Memcpy */
    memcpy(&g_WadHeader, raw, sizeof g_WadHeader);
    return;
  }
  /* PAL (region.c's .wad, the regions plan's §2.3 table): 0 logo, 1 title
   * graphics, 2 title overlay, 3-18 cutscenes, 19 game-over skybox, 20 PETE,
   * 21-92 the levels, 93 credits overlay, 94-113 credits data */
  _Static_assert(PAL_WAD_ENTRIES * 8 <= 2048, "the PAL header is in the sector read");
  memset(&h, 0, sizeof h);
  h.m_UniversalLogo = entry(raw, 0);
  h.m_wad1 = entry(raw, 1);
  h.m_TitleScreenOverlay = entry(raw, r->wad.title_overlay);
  for (i = 0; i < 16; i++)
    g_WadCutscenesPal[i] = entry(raw, r->wad.cutscenes + i);
  for (i = 0; i < 4; i++) /* the title's and English's: what US-shaped readers see */
    h.m_CutsceneData[i] = g_WadCutscenesPal[i];
  h.m_GameOverSkybox = entry(raw, r->wad.gameover_skybox);
  h.m_PETE = entry(raw, r->wad.pete);
  for (i = 0; i < 72; i++)
    if (i % 2)
      h.m_LevelEntry[i / 2].m_Data = entry(raw, r->wad.levels + i);
    else
      h.m_LevelEntry[i / 2].m_Overlay = entry(raw, r->wad.levels + i);
  h.m_CreditsOverlay = entry(raw, r->wad.credits_overlay);
  for (i = 0; i < 10; i++) {
    h.m_Credits1Data[i] = entry(raw, r->wad.credits_data + i);
    h.m_Credits2Data[i] = entry(raw, r->wad.credits_data + 10 + i);
  }
  memcpy(&g_WadHeader, &h, sizeof g_WadHeader);
}

void PortLevelHeader(void *raw_) {
  const unsigned char *raw = raw_;
  LevelHeader h;
  if (g_Region != PSX_REGION_PAL) { /* US: the original Memcpy */
    memcpy(&g_LevelHeader, raw, sizeof g_LevelHeader);
    return;
  }
  /* PAL: 8 words, m_Dragons[5][6] (language major), model offsets, indices */
  memcpy(&h, raw, offsetof(LevelHeader, m_Dragons));
  memcpy(g_LevelDragonsPal, raw + 32, sizeof g_LevelDragonsPal);
  memcpy(h.m_Dragons, g_LevelDragonsPal[(unsigned)g_Language < 5 ? g_Language : 0],
         sizeof h.m_Dragons);
  memcpy(h.m_ModelOffsets, raw + 32 + sizeof g_LevelDragonsPal, sizeof h.m_ModelOffsets);
  memcpy(h.m_ModelIndices, raw + 32 + sizeof g_LevelDragonsPal + sizeof h.m_ModelOffsets,
         sizeof h.m_ModelIndices);
  _Static_assert(offsetof(LevelHeader, m_Dragons) == 32 &&
                     32 + sizeof g_LevelDragonsPal + sizeof h.m_ModelOffsets +
                             sizeof h.m_ModelIndices == PAL_LEVEL_HEADER,
                 "PAL's level header is 656 bytes");
  memcpy(&g_LevelHeader, &h, sizeof g_LevelHeader);
}

/* A PAL table read from the player's executable (wad.h CUTSCENE_VRAM_SIZES,
 * CUTSCENE_PITCHES): its copy, which region_data_load_disc made at start-up */
void *PortRegionData(unsigned pal_addr) {
  void *p = region_data_ptr(pal_addr);
  if (!p)
    PortFatal("PAL data at %08x isn't loaded", pal_addr);
  return p;
}
