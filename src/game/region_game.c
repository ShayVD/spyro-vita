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
OffsetLength g_WadCutscenesPal[16];
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
