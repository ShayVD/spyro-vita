/* The update of two gamestates the decomp hasn't matched to C
 * (asm/nonmatchings/gamestates/update): the dragon rescue (8,
 * func_8002F3E4) and the fairy's save menu (11, func_800314B4), written
 * by hand from the asm so they read as C (docs/native-plan.md stage 3,
 * decisions D87). They replace the mips2c units (cmake/mips2c.cmake
 * M2C_HAND) and are checked against PCSX-Redux with the oracle replay
 * (docs/translation-log.md). M2C_<name>=1 compiles one out for its mips2c
 * unit (cmake SPYRO_M2C_FUNCS, A/B tests). */
#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stddef.h>

#include "common.h"

#include "42CC4.h"
#include "buffers.h"
#include "dragon.h"
#include "hud.h"
#include "images.h"
#include "loaders.h"
#include "memory.h"
#include "camera.h"
#include "collision.h"
#include "fairy.h"
#include "gamepad.h"
#include "gamestates/init.h"
#include "libmcrd.h"
#include "libspu.h"
#include "math.h"
#include "moby_helpers.h"
#include "moby_lists.h"
#include "renderers.h"
#include "overlay_pointers.h"
#include "save_file.h"
#include "sound_table.h"
#include "specular_and_metal.h"
#include "spu.h"
#include "spyro.h"
#include "variables.h"
#include "vector.h"
#include "region.h"
#include "psx_region.h"

extern int g_PalCardHeaderDirty; /* PAL 0x8007C148: the title or pause menu changed the language or screen position (region_game.c) */

void func_80048D10(int pDeltaTime); /* pete_hand.c */
int func_8001796C(int pAngle1, int pAngle2); /* math_hand.c */
/* The mips2c units, linked as <name>_gen with SPYRO_M2C_REF or SPYRO_M2C_FUNCS. */
u_int func_8002F3E4_gen(), func_800314B4_gen();
extern char D_8006FCF4[];           /* a shared work area (ColSphere's contacts) */

static_assert(offsetof(SoundTable, menuCursor) == 0x2D && offsetof(SoundTable, menuConfirm) == 0x2E, "SoundTable");
static_assert(offsetof(Spyro, m_RotationMatrix) == 0x34 && offsetof(Spyro, m_bodyAnimationSpeed) == 0x58, "Spyro");
static_assert(offsetof(Spyro, m_headRotation) == 0x10 && offsetof(Spyro, m_nextBodyAnimation) == 0x19, "Spyro");
static_assert(offsetof(Spyro, m_nextBodyAnimationFrame) == 0x1F && offsetof(Spyro, m_bodyFrameProgress) == 0x24, "Spyro");
static_assert(offsetof(Moby, m_State) == 0x48 && offsetof(Moby, m_Substate) == 0x49, "Moby");
static_assert(offsetof(Spyro, m_floorPositonOnSlope) == 0xA4 && offsetof(Spyro, m_surfaceBelowSpyro) == 0xA0, "Spyro");
static_assert(offsetof(SpyroFlame, m_FairyKissTimer) == 0xA0, "SpyroFlame");
static_assert(offsetof(SoundTable, dragonBreak) == 0x31 && offsetof(SoundTable, dragonCounterBam) == 0x35, "SoundTable");
static_assert(offsetof(__typeof__(g_Spu), m_SoundDefinitions) == 0x2C8 && offsetof(__typeof__(g_Spu), m_SoundTable) == 0x2CC,
              "g_Spu");
static_assert(offsetof(__typeof__(g_Spu), m_NextSoundOverrideFlags) == 0x2D4 &&
                  offsetof(__typeof__(g_Spu), m_VolumeOverride) == 0x2E0,
              "g_Spu");
static_assert(offsetof(__typeof__(g_Hud), m_DragonProgress) == 6 && offsetof(__typeof__(g_Hud), m_DragonCount) == 0x24 &&
                  offsetof(__typeof__(g_Hud), m_Mobys) == 0x44,
              "g_Hud");
static_assert(offsetof(__typeof__(g_DragonCutscene), m_State) == 0x28 &&
                  offsetof(__typeof__(g_DragonCutscene), m_CutsceneTicks) == 0x3C &&
                  offsetof(__typeof__(g_DragonCutscene), m_SoundVoice) == 0x54 &&
                  offsetof(__typeof__(g_DragonCutscene), m_CutsceneSpyro) == 0x88,
              "g_DragonCutscene");
static_assert(offsetof(__typeof__(g_FairyCutscene), unk_0x1c) == 0x1C &&
                  offsetof(__typeof__(g_FairyCutscene), m_CutsceneFairy) == 0x74,
              "g_FairyCutscene");

/* The menu's sounds, 2D. */
static inline void MenuSound(int sound) { PlaySound(sound, NULL, 0x10, NULL); }
#define CURSOR (g_Spu.m_SoundTable->menuCursor)
#define CONFIRM (g_Spu.m_SoundTable->menuConfirm)

/* Shadow runs (host builds with the mips2c units linked, cmake
 * SPYRO_M2C_REF; decisions D87): with g_gs_shadow set (SPYRO_SHADOW=gs),
 * each call first runs the unit in a forked child from the same state,
 * which hands back its RAM and GTE; the hand C then runs here, and RAM
 * (but the scratchpad, the spill area and the emulated stack) and the
 * GTE's control registers must match. A child, not a second run in this
 * process, because these functions act outside RAM too (sounds, the
 * memory card, SPU transfers, the CD loader), which a second run would
 * repeat. Mismatches are printed and counted in g_gs_shadow_fail. */
#define GS_SHADOW (MIPS2C_REPLAY && M2C_REF)
#if GS_SHADOW
#include "mips2c_rt.h"
#include "psx_arena.h"

#include <gtereg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int g_gs_shadow, g_gs_shadow_fail;

typedef struct {
  u_char ram[PSX_RAM_SIZE];
  GTERegisters gte;
} GsShadow;

static GsShadow *s_unit;

/* Runs unit() in a child; 0 if it couldn't. */
static int ShadowUnit(u_int (*unit)()) {
  pid_t pid;
  int status;
  if (!s_unit) {
    s_unit = mmap(NULL, sizeof *s_unit, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s_unit == MAP_FAILED) {
      s_unit = NULL;
      return 0;
    }
  }
  fflush(stdout);
  fflush(stderr);
  pid = fork();
  if (pid == 0) {
    unit();
    memcpy(s_unit->ram, (void *)g_psx_arena_base, PSX_RAM_SIZE);
    s_unit->gte = gteRegs;
    _exit(0);
  }
  return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void ShadowCompare(const char *name, u32 sp) {
  const u32 *h = (const u32 *)g_psx_arena_base, *g = (const u32 *)s_unit->ram;
  u32 i, off;
  int shown = 0;
  for (i = 0; i < PSX_RAM_SIZE / 4; i++) {
    off = i * 4;
    if (h[i] == g[i] || off < 0x10000u || (off >= 0x77DD8u && off < 0x77EA0u) ||
        (off < sp - PSX_RAM_BASE && off >= sp - PSX_RAM_BASE - 0x10000u))
      continue;
    g_gs_shadow_fail++;
    if (shown++ < 8)
      printf("  gsshadow %s ram %08x unit %08x hand %08x\n", name, PSX_RAM_BASE + off, g[i], h[i]);
  }
  for (i = 0; i < 31; i++) /* not FLAG */
    if (gteRegs.CP2C.p[i].d != s_unit->gte.CP2C.p[i].d) {
      g_gs_shadow_fail++;
      printf("  gsshadow %s gte c%u unit %08x hand %08x\n", name, i, s_unit->gte.CP2C.p[i].d, gteRegs.CP2C.p[i].d);
    }
}

#define GS_SHADOW_CALL(name, hand)                                             \
  do {                                                                         \
    if (g_gs_shadow && ShadowUnit(name##_gen)) {                               \
      u32 sp_ = g_mips_sp;                                                     \
      hand();                                                                  \
      ShadowCompare(#name, sp_);                                               \
      return;                                                                  \
    }                                                                          \
  } while (0)
#else
#define GS_SHADOW_CALL(name, hand) ((void)0)
#endif

/* The cutscene's moves: a turn from `from` to `to` (4096ths, the short way
 * round) or a distance or height, eased by ease (0 to 0x2000, a half sine)
 * >> 13. */
static inline int EaseAngle(int from, int to, int ease) {
  int step = (func_80017928(to, from) * ease) >> 13;
  return ((to - from) & 0xFFF) < 0x801 ? from + step : from - step;
}
static inline int EaseValue(int from, int to, int ease) { return from + (((to - from) * ease) >> 13); }

/* A point at (angle, distance) around Spyro, in x and y. */
static inline void AroundSpyro(Vector3D *p, int angle, int distance) {
  p->x = g_Spyro.m_Position.x + ((Cos(angle) * distance) >> 12);
  p->y = g_Spyro.m_Position.y + ((Sin(angle) * distance) >> 12);
}

/* Spyro's animation and the particles, as the play state runs them. */
static inline void SpyroAnimate(void) {
  func_8003CB24(g_Spyro.m_bodyAnimationSpeed);
  func_80049660();
  func_80049E8C();
}

/* The angles that look along dir (a vector from the camera), as the
 * cutscenes compute them: (0, down, around). */
static inline void LookAngles(Vector3D *dir, short angles[3]) {
  angles[0] = 0;
  angles[1] = Atan2(VecMagnitude(dir, 0), -dir->z, 1);
  angles[2] = Atan2(dir->x, dir->y, 1);
}

/* An angle (4096ths) turned towards to by the signed difference x k >>
 * shift. */
static inline short TurnTowards(int from, int to, int k, int shift) {
  return (from + ((func_8001796C(to, (short)from) * k) >> shift)) & 0xFFF;
}

/* ===================================================================== */
/* Gamestate 11: the fairy                                                */
/* ===================================================================== */

#if !M2C_func_800314B4

#define fc g_FairyCutscene

/* The fairy's menu pages (m_MenuDialoguePage). */
enum {
  PAGE_MENU,       /* SAVE / REPLAY / CONTINUE */
  PAGE_NO_CARD,    /* no memory card */
  PAGE_SAVING,     /* the memory card steps below */
  PAGE_CARD_ERROR, /* 3 to 5: errors, then PAGE_RETRY */
  PAGE_RETRY = 6,  /* try again? */
  PAGE_SAVED = 7,
};

/* The first 0x40 ticks: the fairy flies in from where it was to its spot
 * beside Spyro, Sparx and the camera move to theirs, the camera turns to
 * look between Spyro and the fairy, and Spyro turns to the dragon pad;
 * all eased by a half sine. Then the scene is placed where it ends and the
 * menu (state 1) starts. */
static void FairyFlyIn(void) {
  Moby *fairy = fc.m_CutsceneFairy;
  Vector3D look;
  int t = fc.m_AnimationTimer;
  if (t < 0x40) {
    int ease = Sin((t << 5) - 0x400) + 0x1000, angle, distance;
    Moby *sign = &g_LevelMobys[((int *)fairy->m_Props)[1]];
    /* The moby it came from (props word 1) finishes its animation. */
    if (sign->m_State == 2) {
      sign->m_Substate += g_DeltaTime;
      if (sign->m_Substate >= 0x30) {
        sign->m_State = 0;
        sign->m_AnimationState.m_Animation = 0;
      }
    }
    angle = EaseAngle(fc.m_AngleFromSpyro, fc.m_CameraInitAngle, ease);
    distance = EaseValue(fc.m_DistanceFromSpyro, fc.m_CameraInitDistance, ease);
    AroundSpyro(&fairy->m_Position, angle, distance);
    fairy->m_Position.z = EaseValue(fc.m_CameraInitZ, fc.m_CameraTargetZ, ease);
    MobyRotateToSpyro(fairy, 3, 0, 0);
    ColFindFloor(&fairy->m_Position, 0x1000);
    MobyUpdateShadow(fairy);
    if (g_Sparx) {
      angle = EaseAngle(fc.m_CameraTargetAngle, fc.m_CameraEndAngle, ease);
      distance = EaseValue(fc.m_CameraTargetDistance, fc.m_CameraEndDistance, ease);
      AroundSpyro(&g_Sparx->m_Position, angle, distance);
    }
    angle = EaseAngle(fc.m_CameraStartAngle, fc.m_CameraViewAngle, ease);
    distance = EaseValue(fc.m_CameraStartDistance, fc.m_CameraViewDistance, ease);
    AroundSpyro(&g_Camera.m_Position, angle, distance);
    g_Camera.m_Position.z = EaseValue(fc.m_CameraStartZ, fc.m_CameraViewZ, ease);
    /* The camera looks at the point between Spyro and the fairy, 0x240
     * up; for the first 0x20 ticks it turns there gradually. */
    VecAdd(&look, &g_Spyro.m_Position, &fairy->m_Position);
    look.z += 0x240;
    VecShiftRight(&look, 1);
    VecSub(&look, &look, &g_Camera.m_Position);
    if (fc.m_AnimationTimer < 0x20) {
      short ang[3];
      LookAngles(&look, ang);
      g_Camera.m_Rotation.x = TurnTowards(g_Camera.m_Rotation.x, ang[0], fc.m_AnimationTimer, 5);
      g_Camera.m_Rotation.y = TurnTowards(g_Camera.m_Rotation.y, ang[1], fc.m_AnimationTimer, 5);
      g_Camera.m_Rotation.z = TurnTowards(g_Camera.m_Rotation.z, ang[2], fc.m_AnimationTimer, 5);
    } else {
      g_Camera.m_Rotation.x = 0;
      g_Camera.m_Rotation.y = Atan2(VecMagnitude(&look, 0), -look.z, 1);
      g_Camera.m_Rotation.z = Atan2(look.x, look.y, 1);
    }
    g_Spyro.m_bodyRotation.z = EaseAngle(fc.m_unused_SavedSpyroRotZ, fc.m_AngleToDragonPad, ease) >> 4;
    RotVec8ToMatrix(&g_Spyro.m_bodyRotation, &g_Spyro.m_RotationMatrix, NULL);
    g_Spyro.m_headRotation.y = (u_int)ease >> 9;
    func_8003CB24(0x10);
    func_80049660();
    func_80049E8C();
    func_80048D10(g_DeltaTime);
    g_UpdateParticle(g_DeltaTime);
    return;
  }
  AroundSpyro(&fairy->m_Position, fc.m_CameraInitAngle, fc.m_CameraInitDistance);
  fairy->m_Position.z = fc.m_CameraTargetZ;
  fairy->m_Rotation.z = Atan2(g_Spyro.m_Position.x - fairy->m_Position.x, g_Spyro.m_Position.y - fairy->m_Position.y, 0);
  if (g_Sparx)
    AroundSpyro(&g_Sparx->m_Position, fc.m_CameraEndAngle, fc.m_CameraEndDistance);
  {
    int c = Cos(fc.m_CameraViewAngle), s;
    g_Camera.m_Position.x = g_Spyro.m_Position.x + ((c * fc.m_CameraViewDistance) >> 12);
    s = Sin(fc.m_CameraViewAngle);
    g_Spyro.m_bodyFrameProgress = 4;
    g_Spyro.m_nextBodyAnimation = 0;
    g_Spyro.m_nextBodyAnimationFrame = 0;
    g_Spyro.m_bodyAnimationSpeed = 4;
    fc.m_State = 1;
    fc.m_AnimationTimer = 0;
    g_Spyro.m_bodyRotation.z = fc.m_AngleToDragonPad >> 4;
    g_Camera.m_Position.z = fc.m_CameraViewZ;
    g_Camera.m_Position.y = g_Spyro.m_Position.y + ((s * fc.m_CameraViewDistance) >> 12);
  }
}

/* Leaves the cutscene with the confirm sound. */
static void FairyExit(void) {
  func_8002D02C();
  MenuSound(CONFIRM);
}

/* PAL (PAL 0x800373C8-0x80037470, D139): once a save has been read back
 * good, if the title screen changed the language or the pause menu the
 * screen position, the card file's 256-byte header is written again: PAL's
 * template (0x80074C24, the standard header: "SC", one icon frame, one block)
 * copied to a buffer (PAL's is 0x80076298), the language and screen position packed
 * into the halfword at +0x7C (the icon palette's colour 14) and the save's id
 * into +0x7E (colour 15, the id the read above checks). MemCardSync(0) waits. */
static void PalCardHeaderWrite(void) {
  /* PAL's buffer is a global (0x80076298); here a local, as the port's
   * MemCardWriteFile writes before it returns, so save states have nothing to keep */
  u_int words[0x100 / 4];
  u_char *hdr = (u_char *)words;
  g_PalCardHeaderDirty = 0;
  Memcpy(hdr, PortRegionData(0x80074C24), 0x100);
  *(u_short *)(hdr + 0x7C) = g_Language + (g_ScreenOffsetX << 4) + (g_ScreenOffsetY << 10);
  *(u_short *)(hdr + 0x7E) = fc.unk_0x20;
  MemCardWriteFile(fc.m_MemoryCardSlot, (char *)psx_region()->card, words, 0, 0x100);
  MemCardSync(0, &fc.unk_0x24, &fc.unk_0x28);
}

/* The save itself, a step a tick (m_MenuSelectedOption counts them):
 * open the card, read the save file's header, check it's this game's
 * (the save's id at 0x7E, unk_0x20), write the save, read it back and
 * check its checksum. A failed step shows an error page. MemCardSync's
 * result goes to unk_0x28 (its command to unk_0x24). */
static void FairySave(void) {
  /* the release's card file: BASCUS-94228SPYRO, or PAL's BESCES-01438SPYRO
   * (PAL 0x800149F0; src/platform/common/region.c) */
  const char *name = psx_region()->card;
  u_char *save = (u_char *)g_Buffers.m_HudOTStart - 0x600;
  int r, page;
  switch (fc.m_MenuSelectedOption) {
  case 0:
    MemCardStart();
    MemCardAccept(fc.m_MemoryCardSlot);
    fc.m_MenuSelectedOption = 1;
    return;
  case 1:
    if (!MemCardSync(1, &fc.unk_0x24, &fc.unk_0x28))
      return;
    r = fc.unk_0x28;
    if (r == 0 || r == 3) {
      MemCardReadFile(fc.m_MemoryCardSlot, (char *)name, (u_int *)save, 0, 0x80);
      fc.m_MenuSelectedOption = 2;
      return;
    }
    page = r == 4 ? 4 : 3;
    break;
  case 2:
    if (!MemCardSync(1, &fc.unk_0x24, &fc.unk_0x28))
      return;
    r = fc.unk_0x28;
    if (r != 0) {
      page = r == 5 ? 4 : 5;
    } else if (fc.unk_0x20 != *(u_short *)(save + 0x7E)) {
      page = 4;
    } else {
      SaveCreate((SaveFile *)save);
      MemCardWriteFile(fc.m_MemoryCardSlot, (char *)name, (u_int *)save, fc.unk_0x1c * 0x600 + 0x200, 0x600);
      fc.m_MenuSelectedOption = 3;
      return;
    }
    break;
  case 3:
    if (!MemCardSync(1, &fc.unk_0x24, &fc.unk_0x28))
      return;
    if (fc.unk_0x28 == 0) {
      MemCardReadFile(fc.m_MemoryCardSlot, (char *)name, (u_int *)save, fc.unk_0x1c * 0x600 + 0x200, 0x600);
      fc.m_MenuSelectedOption = 4;
      return;
    }
    page = 5;
    break;
  default:
    if (!MemCardSync(1, &fc.unk_0x24, &fc.unk_0x28))
      return;
    page = fc.unk_0x28 == 0 && SaveChecksum(save) == *(int *)(save + 0x58C) ? PAGE_SAVED : 5;
    if (page == PAGE_SAVED && REGION_IS(PAL) && g_PalCardHeaderDirty)
      PalCardHeaderWrite();
    break;
  }
  fc.m_MenuDialoguePage = page;
  fc.m_AnimationTimer = 0;
}

/* The menu (state 1): Spyro and the particles go on; the page's input. */
static void FairyMenu(void) {
  SpyroAnimate();
  g_GameTick++;
  g_UpdateParticle(g_DeltaTime);
  switch (fc.m_MenuDialoguePage) {
  case PAGE_MENU:
    if (g_Pad.m_Down & PAD_DOWN) {
      MenuSound(CURSOR);
      fc.m_AnimationTimer = 0;
      if (++fc.m_MenuSelectedOption >= 3)
        fc.m_MenuSelectedOption = 0;
    } else if (g_Pad.m_Down & PAD_UP) {
      MenuSound(CURSOR);
      fc.m_AnimationTimer = 0;
      if (--fc.m_MenuSelectedOption < 0)
        fc.m_MenuSelectedOption = 2;
    }
    if (fc.m_AnimationTimer < 8 || !(g_Pad.m_Down & PAD_CROSS))
      return;
    MenuSound(CONFIRM);
    if (fc.m_MenuSelectedOption == 0) {
      fc.m_AnimationTimer = 0;
      fc.m_MenuDialoguePage = fc.m_HasMemoryCard ? PAGE_SAVING : PAGE_NO_CARD;
    } else if (fc.m_MenuSelectedOption == 1) {
      /* Replay the level's last dragon: if the dragon (props word 0) has
       * been freed and can be again, put it back. */
      Moby *dragon = &g_LevelMobys[*(int *)fc.m_CutsceneFairy->m_Props];
      int *props = dragon->m_Props;
      if (props[9] != -1 || props[6] == -1)
        return;
      func_8002D02C();
      dragon->m_State = 2;
      dragon->m_CollisionGroup = NULL;
      dragon->m_CollisionRegion = -1;
      func_8002C924(dragon);
      g_DragonTotal--;
      g_LevelDragonCount[g_LevelIndex]--;
    } else {
      FairyExit();
    }
    return;
  case PAGE_NO_CARD:
    if (fc.m_AnimationTimer < 0x3C || !(g_Pad.m_Down & PAD_CROSS))
      return;
    MenuSound(CONFIRM);
    fc.m_AnimationTimer = 0;
    fc.m_MenuDialoguePage = PAGE_MENU;
    fc.m_MenuSelectedOption = 2;
    return;
  case PAGE_SAVING:
    FairySave();
    return;
  case 3:
  case 4:
  case 5:
    if (fc.m_AnimationTimer < 0x3C || !(g_Pad.m_Down & PAD_CROSS))
      return;
    MenuSound(CONFIRM);
    fc.m_AnimationTimer = 0;
    fc.m_MenuDialoguePage = PAGE_RETRY;
    fc.m_MenuSelectedOption = 0;
    return;
  case PAGE_RETRY:
    if (g_Pad.m_Down & (PAD_UP | PAD_DOWN)) {
      MenuSound(CURSOR);
      fc.m_MenuSelectedOption = 1 - fc.m_MenuSelectedOption;
    }
    if (fc.m_AnimationTimer < 0x20 || !(g_Pad.m_Down & PAD_CROSS))
      return;
    MemCardStop();
    if (fc.m_MenuSelectedOption != 0) {
      FairyExit();
      return;
    }
    MenuSound(CONFIRM);
    fc.m_AnimationTimer = 0;
    fc.m_MenuDialoguePage = PAGE_SAVING;
    fc.m_MenuSelectedOption = 0;
    return;
  case PAGE_SAVED:
    if (fc.m_AnimationTimer < 8 || !(g_Pad.m_Down & PAD_CROSS))
      return;
    MemCardStop();
    FairyExit();
    return;
  }
}

static void Fairy(void) {
  Moby **list = (Moby **)(D_8006FCF4 + 0x400);
  fc.m_AnimationTimer += g_DeltaTime;
  SpecularUpdate(3);
  list[1] = NULL;
  list[0] = fc.m_CutsceneFairy;
  func_800522C0(list, 0);
  if (fc.m_State == 0)
    FairyFlyIn();
  else if (fc.m_State == 1)
    FairyMenu();
}

void func_800314B4(void) {
  GS_SHADOW_CALL(func_800314B4, Fairy);
  Fairy();
}

#undef fc
#endif

/* ===================================================================== */
/* Gamestate 8: the dragon rescue                                         */
/* ===================================================================== */

#if !M2C_func_8002F3E4

#define dc g_DragonCutscene

extern u_char D_8006F390[8];    /* the freed dragon's wobble while Spyro walks up: (x, y) per step */
extern u_char D_8006F398[8];    /* the same while it speaks */
extern Vector3D D_8006F3F0[3];  /* the HUD's three mobys' places as the cutscene ends */
void func_8002CB6C(void);       /* gamestates/init.c: back to play */
void func_80049FAC(int);        /* pete_hand.c */

/* The rescue's states (m_State). */
enum {
  DRAGON_FREE,    /* the crystal shatters; Spyro is placed */
  DRAGON_WALK_UP, /* Spyro walks to the dragon and turns */
  DRAGON_WAIT,    /* waiting for the cutscene's data */
  DRAGON_END = 7, /* fade back to the level */
};

/* The screen effect (D_80076248): on, its byte 0x2B (after vec_0x28),
 * its strength word and its turn. */
static void DragonFlash(int b2b, int strength, int turn) {
  D_80076248.unk_0x0 = 1;
  ((u_char *)&D_80076248.vec_0x28)[3] = b2b;
  D_80076248.unk_0x24 = strength;
  D_80076248.vec_0x28.z += turn;
}

/* The freed dragon wobbles, by a table of (x, y) turns per step, and the
 * crystal's pieces fly (class 0xFB) with the pad rumbling. */
static void DragonWobble(Moby *dragon, const u_char *steps) {
  dragon->m_Substate = (dragon->m_Substate + 1) & 3;
  dragon->m_Rotation.x = steps[dragon->m_Substate * 2];
  dragon->m_Rotation.y = steps[dragon->m_Substate * 2 + 1];
  if (D_800757D0 < 8)
    D_800757D0 = 8;
  D_8007584C = 0x46;
  g_SpawnMoby(0xFB, dragon);
}


/* A moby's animation frame from the cutscene's ticks, at the rate its
 * animation's ticks per frame give (0x10: 8 ticks a frame, 0x15: 6, 0x20:
 * 4, else 2), held on the last frame. */
static void CutsceneAnimate(Moby *m, int model, int ticks) {
  const AnimationHeader *anim = g_Models[model]->m_Animations[0];
  int p = anim->m_ProgressPerTick, n = p == 0x10 ? 8 : p == 0x15 ? 6 : p == 0x20 ? 4 : 2;
  /* PAL (0x80035C08): the dragon's model at 0 ticks per frame holds its
   * first frame (D139) */
  if (REGION_IS(PAL) && model == 0x1FF && p == 0)
    n = 0x100;
  AnimationState *a = &m->m_AnimationState;
  a->m_Frame = ticks / n;
  a->m_NextFrame = a->m_Frame + 1;
  a->m_FrameProgress = ((ticks % n) << 6) / n;
  if (a->m_NextFrame >= anim->m_NumFrames) {
    a->m_Frame = anim->m_NumFrames - 1;
    a->m_FrameProgress = 0;
  }
}

/* The cutscene camera's frame k (unk_0x48: per two ticks a position and
 * three angles, as words). */
static void CutsceneCamera(int k) {
  const int *f = (const int *)dc.unk_0x48 + k * 6;
  g_Camera.m_Position.x = f[0], g_Camera.m_Position.y = f[1], g_Camera.m_Position.z = f[2];
  g_Camera.m_Rotation.x = f[3], g_Camera.m_Rotation.y = f[4], g_Camera.m_Rotation.z = f[5];
}

/* The HUD's dragon icons (its mobys 5 to 7) back at their places, then
 * moved down by y. */
static void HudDragonMobys(int y) {
  int i;
  for (i = 0; i < 3; i++) {
    VecCopy(&g_Hud.m_Mobys[5 + i].m_Position, &D_8006F3F0[i]);
    g_Hud.m_Mobys[5 + i].m_Position.y += y;
  }
}

static void DragonSound(int sound) { PlaySound(sound, NULL, 0x10, (u_char *)&dc.m_SoundVoice); }

/* DRAGON_FREE: the crystal flashes; at 0x30 ticks the walk-up starts from
 * where Spyro, the camera and the dragon's pad (its props: a position, the
 * walk's end angle and distance, ...) are. */
static void DragonFree(void) {
  Moby *dragon = dc.m_RescuedDragonMoby;
  Vector3D v, *pad;
  int t;
  g_GameTick++;
  g_UpdateMoby();
  func_8004A200();
  CameraUpdate();
  t = dc.m_CutsceneTicks;
  DragonFlash((t << 2) + 0x40, t << 4, ((t >> 4) + 2) * g_DeltaTime);
  dc.m_Fade = t < 0x21 ? 0 : (t - 0x20) << 4;
  if (D_800757D0 < 8)
    D_800757D0 = 8;
  D_8007584C = 0x46;
  if (dc.m_CutsceneTicks < 0x30)
    return;
  dc.m_State = DRAGON_WALK_UP;
  dc.m_CutsceneTicks = 0;
  dc.m_Fade = 0xFF;
  dragon->m_Position.z -= 0x78;
  /* Spyro's, the camera's and the pad's angle and distance from the
   * dragon; Spyro's end angle and distance. */
  dc.unk_0x60 = Atan2(g_Spyro.m_Position.x - dragon->m_Position.x, g_Spyro.m_Position.y - dragon->m_Position.y, 1);
  VecSub(&v, &g_Spyro.m_Position, &dragon->m_Position);
  dc.unk_0x64 = VecMagnitude(&v, 0);
  dc.unk_0x68 = ((int *)dragon->m_Props)[10];
  dc.unk_0x6C = ((int *)dragon->m_Props)[11];
  dc.unk_0x70 = Atan2(g_Camera.m_Position.x - dragon->m_Position.x, g_Camera.m_Position.y - dragon->m_Position.y, 1);
  VecSub(&v, &g_Camera.m_Position, &dragon->m_Position);
  dc.unk_0x74 = VecMagnitude(&v, 0);
  dc.unk_0x78 = g_Camera.m_Position.z;
  pad = (Vector3D *)dragon->m_Props;
  dc.unk_0x7C = Atan2(pad->x - dragon->m_Position.x, pad->y - dragon->m_Position.y, 1);
  VecSub(&v, pad, &dragon->m_Position);
  dc.unk_0x80 = VecMagnitude(&v, 0);
  dc.unk_0x84 = pad->z;
  /* The walk's length: 0x28 + the turn / 16, 0x40 to 0x80 ticks. */
  dc.unk_0x40 = (func_80017928(dc.unk_0x60, dc.unk_0x68) >> 4) + 0x28;
  if (dc.unk_0x40 < 0x40)
    dc.unk_0x40 = 0x40;
  if (dc.unk_0x40 > 0x80)
    dc.unk_0x40 = 0x80;
  g_SavedFairyKissTimer = g_SpyroFlame.m_FairyKissTimer;
  func_8004AC24(1);
  g_Spyro.m_bodyAnimation = 1;
  g_Spyro.m_nextBodyAnimation = 1;
  g_Spyro.m_nextBodyAnimationFrame = 1;
  g_Spyro.m_bodyAnimationFrame = 0;
  g_Spyro.m_bodyFrameProgress = 0;
  g_ParticleAllocPtr = g_Particles;
  g_Particles->m_RenderType = 0xFF;
  KillSoundsAndMusic(1 << dragon->m_SoundChannel);
  DragonSound(g_Spu.m_SoundTable->dragonRumbleBreaking);
}

/* DRAGON_WALK_UP: Spyro walks round to the dragon (unk_0x40 ticks), the
 * camera to the pad; then turns to face it (0x20 ticks). Spyro's tilt
 * follows the ground. */
static void DragonWalkUp(void) {
  Moby *dragon = dc.m_RescuedDragonMoby;
  int t = dc.m_CutsceneTicks, len = dc.unk_0x40;
  Vector3D v;
  if (t < 0x20)
    DragonFlash(((0x20 - t) << 2) + 0x40, (0x20 - t) << 4, (((0x20 - t) >> 4) + 2) * g_DeltaTime);
  else
    D_80076248.unk_0x0 = 0;
  dc.m_Fade = dc.m_CutsceneTicks < 8 ? (8 - dc.m_CutsceneTicks) << 5 : 0;
  DragonWobble(dragon, D_8006F390);
  t = dc.m_CutsceneTicks;
  if (t < len) {
    int ease = Sin((t << 11) / len - 0x400) + 0x1000, angle, dist, heading, speed;
    angle = EaseAngle(dc.unk_0x60, dc.unk_0x68, ease);
    dist = dc.unk_0x64 + (((dc.unk_0x6C - dc.unk_0x64) * (Sin((dc.m_CutsceneTicks << 10) / dc.unk_0x40 - 0x400) + 0x1000)) >> 12);
    VecCopy(&v, &g_Spyro.m_Position);
    g_Spyro.m_Position.x = dragon->m_Position.x + ((Cos(angle) * dist) >> 12);
    g_Spyro.m_Position.y = dragon->m_Position.y + ((Sin(angle) * dist) >> 12);
    VecSub(&v, &g_Spyro.m_Position, &v);
    /* Spyro turns a quarter of the way to where he's going, 8-bit. */
    heading = Atan2(v.x, v.y, 0);
    if (func_80017908(heading, g_Spyro.m_bodyRotation.z) >= 4) {
      if (((heading - g_Spyro.m_bodyRotation.z) & 0xFF) < 0x81)
        g_Spyro.m_bodyRotation.z += func_80017908(heading, g_Spyro.m_bodyRotation.z) >> 2;
      else
        g_Spyro.m_bodyRotation.z -= func_80017908(heading, g_Spyro.m_bodyRotation.z) >> 2;
    }
    speed = VecMagnitude(&v, 0) >> 1;
    g_Spyro.m_bodyAnimationSpeed = speed < 4 ? 4 : speed > 0x10 ? 0x10 : speed;
    angle = EaseAngle(dc.unk_0x70, dc.unk_0x7C, ease);
    dist = EaseValue(dc.unk_0x74, dc.unk_0x80, ease);
    g_Camera.m_Position.x = dragon->m_Position.x + ((Cos(angle) * dist) >> 12);
    g_Camera.m_Position.y = dragon->m_Position.y + ((Sin(angle) * dist) >> 12);
    g_Camera.m_Position.z = EaseValue(dc.unk_0x78, dc.unk_0x84, ease);
    /* 0x10 ticks before he arrives, the stop animation. */
    if (dc.m_CutsceneTicks >= dc.unk_0x40 - 0x10 && dc.m_CutsceneTicks - g_DeltaTime < dc.unk_0x40 - 0x10) {
      int frame = g_Spyro.m_nextBodyAnimationFrame;
      g_Spyro.m_nextBodyAnimation = 3;
      g_Spyro.m_nextBodyAnimationFrame = 0;
      g_Spyro.m_bodyFrameProgress = 0;
      g_Spyro.m_bodyAnimationSpeed = 0xC;
      g_Spyro.m_bodyAnimationFrame = frame;
    }
  } else if (t < len + 0x20) {
    int d, a;
    if (t - g_DeltaTime < len) {
      /* Arrived: at his place exactly, the camera at the pad; he turns
       * from where he faces to the other way from his end angle. */
      int s;
      g_Spyro.m_Position.x = dragon->m_Position.x + ((Cos(dc.unk_0x68) * dc.unk_0x6C) >> 12);
      s = Sin(dc.unk_0x68);
      dc.unk_0x60 = g_Spyro.m_bodyRotation.z << 4;
      dc.unk_0x68 = (dc.unk_0x68 + 0x800) & 0xFFF;
      g_Spyro.m_Position.y = dragon->m_Position.y + ((s * dc.unk_0x6C) >> 12);
      VecCopy(&g_Camera.m_Position, (Vector3D *)dragon->m_Props);
    }
    d = func_80017928(dc.unk_0x68, dc.unk_0x60) * (dc.m_CutsceneTicks - dc.unk_0x40) / 32;
    a = ((dc.unk_0x68 - dc.unk_0x60) & 0xFFF) < 0x801 ? dc.unk_0x60 + d : dc.unk_0x60 - d;
    g_Spyro.m_bodyRotation.z = a >> 4;
    g_Spyro.m_bodyAnimationSpeed = 8;
  } else {
    dc.m_State = DRAGON_WAIT;
    g_Spyro.m_bodyFrameProgress = 4;
    dc.m_CutsceneTicks = 0;
    g_Spyro.m_nextBodyAnimation = 0;
    g_Spyro.m_nextBodyAnimationFrame = 0;
    g_Spyro.m_bodyAnimationSpeed = 4;
    g_Spyro.m_bodyRotation.z = dc.unk_0x68 >> 4;
  }
  if (dc.m_State == DRAGON_WALK_UP) {
    /* The camera looks between Spyro and the dragon: turning there for
     * 0x10 ticks, then straight at it, then for the last 0x20 ticks of
     * the walk turning on to the pad's angles (props words 3-5). */
    short ang[3];
    VecAdd(&v, &g_Spyro.m_Position, &dragon->m_Position);
    VecShiftRight(&v, 1);
    VecSub(&v, &v, &g_Camera.m_Position);
    t = dc.m_CutsceneTicks;
    if (t < 0x10) {
      LookAngles(&v, ang);
      g_Camera.m_Rotation.x = TurnTowards(g_Camera.m_Rotation.x, ang[0], dc.m_CutsceneTicks, 4);
      g_Camera.m_Rotation.y = TurnTowards(g_Camera.m_Rotation.y, ang[1], dc.m_CutsceneTicks, 4);
      g_Camera.m_Rotation.z = TurnTowards(g_Camera.m_Rotation.z, ang[2], dc.m_CutsceneTicks, 4);
    } else if (t < dc.unk_0x40 - 0x20) {
      g_Camera.m_Rotation.x = 0;
      g_Camera.m_Rotation.y = Atan2(VecMagnitude(&v, 0), -v.z, 1);
      g_Camera.m_Rotation.z = Atan2(v.x, v.y, 1);
    } else {
      const int *props = dragon->m_Props;
      LookAngles(&v, ang);
      g_Camera.m_Rotation.x = TurnTowards((u_short)ang[0], props[3], dc.m_CutsceneTicks - dc.unk_0x40 + 0x20, 6);
      g_Camera.m_Rotation.y = TurnTowards((u_short)ang[1], props[4], dc.m_CutsceneTicks - dc.unk_0x40 + 0x20, 6);
      g_Camera.m_Rotation.z = TurnTowards((u_short)ang[2], props[5], dc.m_CutsceneTicks - dc.unk_0x40 + 0x20, 6);
    }
  }
  {
    /* Spyro's tilt: the floor's slope (m_floorPositonOnSlope) turned into
     * his heading. */
    int r = g_Spyro.m_bodyRotation.z << 4, c = Cos(r), s = Sin(r), a;
    const Vector3D *n = &g_Spyro.m_floorPositonOnSlope;
    v.x = (n->x * c + n->y * s) >> 12;
    c = Cos(g_Spyro.m_bodyRotation.z << 4), s = Sin(g_Spyro.m_bodyRotation.z << 4);
    v.z = n->z;
    v.y = (n->y * c - n->x * s) >> 12;
    a = func_80017A38(v.x * v.x + n->z * n->z);
    g_Spyro.m_bodyRotation.x = -Atan2(a, v.y, 1);
    g_Spyro.m_bodyRotation.y = -Atan2(v.z, v.x, 1);
  }
  SpyroAnimate();
  g_Spyro.m_Position.z += 0x400;
  g_Spyro.m_surfaceBelowSpyro = func_8004DF24(&g_Spyro.m_Position);
  g_Spyro.m_Position.z = g_Spyro.m_surfaceBelowSpyro + 0x164;
  func_80049FAC(0);
}

/* DRAGON_WAIT: until the cutscene's data has loaded (m_Stage 3), and at
 * least 0x10 ticks; then the crystal shatters, the cutscene's dragon and
 * Spyro mobys replace the level's, and the dragon's line plays. */
static void DragonWait(void) {
  Moby *dragon = dc.m_RescuedDragonMoby, *m;
  int i;
  DragonWobble(dragon, D_8006F398);
  if (dc.m_CutsceneTicks < 0x10 || dc.m_Stage < 3) {
    SpyroAnimate();
    return;
  }
  dc.m_State = 3;
  dc.m_CutsceneTicks = 0;
  for (i = 0; i < 0x20; i++)
    g_SpawnMoby(0xFB, dc.m_RescuedDragonMoby);
  /* The decomp's names are the wrong way round: m_CutsceneSpyro is the
   * cutscene's dragon (class 0x1FE), m_CutsceneDragon its Spyro (0x1FF). */
  m = dc.m_CutsceneSpyro = MobyAlloc();
  func_8003A720(m);
  dc.m_CutsceneSpyro->m_Class = 0x1FE;
  VecCopy(&dc.m_CutsceneSpyro->m_Position, &dc.m_RescuedDragonMoby->m_Position);
  dc.m_CutsceneSpyro->m_Rotation.z = dc.m_RescuedDragonMoby->m_Rotation.z;
  dc.m_CutsceneSpyro->m_ScaleOverride = 0x60;
  dc.m_CutsceneSpyro->m_DepthOffset = 5;
  dc.m_CutsceneSpyro->m_ShadowDistance = -1;
  dc.m_CutsceneSpyro->m_Position.z += 0x400;
  if (REGION_IS(PAL)) /* PAL (0x800355B8): the floor under him kept (D139) */
    g_CollisionPoint.z = func_8004DF24(&dc.m_CutsceneSpyro->m_Position);
  else
    func_8004DF24(&dc.m_CutsceneSpyro->m_Position);
  dc.m_CutsceneSpyro->m_Position.z -= 0x400;
  MobyUpdateShadow(dc.m_CutsceneSpyro);
  MobyUpdateCollision(dc.m_CutsceneSpyro);
  m = MobyAlloc();
  dc.m_CutsceneDragon = m;
  func_8003A720(m);
  dc.m_CutsceneDragon->m_Class = 0x1FF;
  VecCopy(&dc.m_CutsceneDragon->m_Position, &g_Spyro.m_Position);
  dc.m_CutsceneDragon->m_DepthOffset = 5;
  MobyUpdateCollision(dc.m_CutsceneDragon);
  StopSound(dc.m_SoundVoice, 4);
  DragonSound(g_Spu.m_SoundTable->dragonBreak);
  dc.m_RescuedDragonMoby->m_Position.z += 0x78;
  MobyFree(dc.m_RescuedDragonMoby);
}

/* 3: the cutscene dragon grows to its size and turns to its angle (props
 * word 12) over 0x18 ticks. */
static void DragonAppear(void) {
  Moby *dragon = dc.m_RescuedDragonMoby;
  int t = dc.m_CutsceneTicks;
  if (t < 0x18) {
    int d;
    dc.m_CutsceneSpyro->m_ScaleOverride = 0x60 - t * 3;
    d = func_80017948(((int *)dragon->m_Props)[12] >> 4, dragon->m_Rotation.z);
    dc.m_CutsceneSpyro->m_Rotation.z = dragon->m_Rotation.z + d * dc.m_CutsceneTicks / 24;
    return;
  }
  dc.m_State = 4;
  dc.m_CutsceneTicks = 0;
  dc.m_CutsceneSpyro->m_Rotation.z = ((int *)dragon->m_Props)[12] >> 4;
  dc.m_CutsceneSpyro->m_ScaleOverride = 0;
  dc.m_CutsceneDragon->m_Rotation.x = g_Spyro.m_bodyRotation.x;
  dc.m_CutsceneDragon->m_Rotation.y = g_Spyro.m_bodyRotation.y;
  dc.m_CutsceneDragon->m_Rotation.z = g_Spyro.m_bodyRotation.z;
}

/* 4: the cutscene proper. The camera and both mobys' frames follow the
 * cutscene's data; the dragon's speech starts at its props word 16's tick
 * (an SPU sample at the top of SPU RAM, played through sound 0's
 * definition, borrowed). X skips it when the dragon allows (props word
 * 13 0), or shows the skip prompt for 0x78 ticks (unk_0x5C). At the end,
 * the level's sounds go back into SPU RAM. */
/* The tick the dragon's voice starts at: props word 16; PAL (0x80035800,
 * D139) takes words 17-20 for French, German, Spanish and Italian unless
 * -1 (each dub's own timing, set by func_8002C924) and scales it to 25 Hz
 * ticks, (t * 5 + 3) / 6 from 6 on (RT: US timing in PAL 60). */
static int DragonVoiceDelay(Moby *dragon) {
  int *props = dragon->m_Props, t = props[16];
  if (REGION_IS(PAL)) {
    if (g_Language >= 1 && g_Language <= 4 && props[16 + g_Language] != -1)
      t = props[16 + g_Language];
    if (RT(0, 1) && t >= 6)
      t = (t * 5 + 3) / 6;
  }
  return t;
}

static void DragonCutscene(void) {
  Moby *dragon = dc.m_RescuedDragonMoby;
  int skip = 0;
  if (dc.m_CutsceneTicks < dc.unk_0x4C * 2) {
    if (dc.unk_0x58 == 0 && dc.m_CutsceneTicks >= DragonVoiceDelay(dragon)) {
      SoundDefinition saved, *def;
      Memcpy(&saved, g_Spu.m_SoundDefinitions, sizeof saved);
      def = g_Spu.m_SoundDefinitions;
      def->m_Addr = 0x80000 - dc.m_Header.m_SpuData.m_Length;
      def->m_LoopAddr = -1;
      def->unk_0x8 = 0x32;
      def->m_Pitch = (((int *)dragon->m_Props)[15] << 10) / 11025;
      def->m_PitchVariance = 0;
      def->m_PitchMultiplier = 0;
      def->m_VarianceType = 0;
      g_Spu.m_NextSoundOverrideFlags = 1;
      g_Spu.m_VolumeOverride.left = 0x3FFF;
      g_Spu.m_VolumeOverride.right = 0x3FFF;
      StopSound(dc.m_SoundVoice, 4);
      DragonSound(0);
      Memcpy(g_Spu.m_SoundDefinitions, &saved, sizeof saved);
      dc.unk_0x58 = 1;
    }
    CutsceneCamera(dc.m_CutsceneTicks >> 1);
    CutsceneAnimate(dc.m_CutsceneSpyro, 0x1FE, dc.m_CutsceneTicks);
    CutsceneAnimate(dc.m_CutsceneDragon, 0x1FF, dc.m_CutsceneTicks);
    if (g_Pad.m_Down & PAD_CROSS) {
      if (((int *)dragon->m_Props)[13] == 0)
        skip = 1;
      else if (dc.unk_0x5C == 0)
        dc.unk_0x5C = 1;
    }
  }
  if (skip || dc.m_CutsceneTicks >= dc.unk_0x4C * 2) {
    dc.m_State = 5;
    dc.m_CutsceneTicks = 0;
    CutsceneCamera(dc.unk_0x4C - 1);
    dc.m_CutsceneSpyro->m_AnimationState.m_Frame = (u_char)g_Models[0x1FE]->m_Animations[0]->m_NumFrames - 1;
    dc.m_CutsceneSpyro->m_AnimationState.m_FrameProgress = 0;
    dc.m_CutsceneDragon->m_AnimationState.m_Frame = (u_char)g_Models[0x1FF]->m_Animations[0]->m_NumFrames - 1;
    dc.m_CutsceneDragon->m_AnimationState.m_FrameProgress = 0;
    g_Spyro.m_bodyAnimation = 0;
    g_Spyro.m_nextBodyAnimation = 0;
    g_Spyro.m_bodyAnimationFrame = 0;
    g_Spyro.m_nextBodyAnimationFrame = 1;
    g_Spyro.m_bodyFrameProgress = 0;
    g_Spyro.m_bodyAnimationSpeed = 4;
    func_80049660();
    func_80049E8C();
    StopSound(dc.m_SoundVoice, 2);
    SpuUpdate();
    if (!dc.m_HasOverflow) {
      SpuSetTransferStartAddr(0x80000 - dc.m_Header.m_SpuData.m_Length);
      SpuWrite((u_char *)g_Buffers.m_ModelData + dc.m_Header.m_SpuData.m_Offset, dc.m_Header.m_SpuData.m_Length);
      while (!SpuIsTransferCompleted(0))
        ;
    }
  }
  if (dc.unk_0x5C != 0) {
    dc.unk_0x5C += g_DeltaTime;
    if (dc.unk_0x5C >= 0x78)
      dc.unk_0x5C = 0;
  }
}

/* 5: the cutscene dragon fades to white (its vertex colours lerped) and
 * the HUD's dragon counter slides in; at 0x19 ticks the dragon vanishes in
 * a burst and both cutscene mobys go. */
static void DragonVanish(void) {
  AnimationHeader *anim = g_Models[0x1FE]->m_Animations[0];
  Vector3D v;
  int i;
  for (i = 0; i < (short)anim->m_NumColors; i++)
    ((int *)anim->m_Colors)[i] = ColorLerp(((int *)anim->m_Colors)[i], 0xFFFFFF, 0x200);
  SpyroAnimate();
  HudDragonMobys(((dc.m_CutsceneTicks < 0x19 ? g_HudOpeningOffsets[dc.m_CutsceneTicks >> 1] : g_HudOpeningOffsets[12]) * 3) >> 1);
  if (dc.m_CutsceneTicks < 0x19)
    return;
  dc.m_State = 6;
  dc.m_CutsceneTicks = 0;
  g_Hud.m_DragonProgress = 0;
  VecCopy(&v, &dc.m_CutsceneSpyro->m_Position);
  v.z += 0x40;
  g_SpawnParticle(0x20, 0x18, &v, 0);
  VecCopy(&D_80076248.vec_0x04, &v);
  MobyFree(dc.m_CutsceneSpyro);
  MobyFree(dc.m_CutsceneDragon);
  DragonSound(g_Spu.m_SoundTable->dragonCounter);
}

/* 6: the HUD's dragon count goes up (its icon spins once, 0x10 a tick,
 * with a bam at the start; the count steps at 0xC0); after 0x60 ticks,
 * once the level is back (m_Stage 6), the flash back. */
static void DragonCount(void) {
  SpyroAnimate();
  if (g_Hud.m_DragonCount != g_DragonTotal || g_Hud.m_DragonProgress != 0) {
    int count = g_Hud.m_DragonCount;
    g_Hud.m_DragonProgress += 0x10;
    HudPrint(5, 2, count + 1, 0);
    HudMobyRotate(5, 2, g_Hud.m_DragonProgress);
    if (g_Hud.m_DragonProgress == 0xC0)
      g_Hud.m_DragonCount++;
    if (g_Hud.m_DragonProgress == 0)
      DragonSound(g_Spu.m_SoundTable->dragonCounterBam);
  }
  if (dc.m_CutsceneTicks < 0x60 || dc.m_Stage != 6)
    return;
  dc.m_State = DRAGON_END;
  dc.m_CutsceneTicks = 0;
  g_ParticleAllocPtr = g_Particles;
  g_Particles->m_RenderType = 0xFF;
  DragonSound(g_Spu.m_SoundTable->dragonFlash);
}

/* DRAGON_END: the HUD's icons drop back for 0x10 ticks; the flash fades
 * over 0x20, then play resumes. */
static void DragonEnd(void) {
  int k;
  SpyroAnimate();
  if (dc.m_CutsceneTicks < 0x10)
    HudDragonMobys((0x11 - dc.m_CutsceneTicks) * 3);
  k = dc.m_CutsceneTicks < 0x10 ? dc.m_CutsceneTicks : 0x20 - dc.m_CutsceneTicks;
  dc.m_Fade = (k << 4) - 1;
  DragonFlash((k << 3) + 0x40, k * 0x50, 8);
  if (dc.m_CutsceneTicks >= 0x20) {
    D_80076248.unk_0x0 = 0;
    func_8002CB6C();
  }
}

static void Dragon(void) {
  Moby *m;
  dc.m_CutsceneTicks += g_DeltaTime;
  dc.unk_0x50 += g_DeltaTime;
  switch (dc.m_State) {
  case DRAGON_FREE:
    DragonFree();
    break;
  case DRAGON_WALK_UP:
    DragonWalkUp();
    break;
  case DRAGON_WAIT:
    DragonWait();
    break;
  case 3:
    DragonAppear();
    break;
  case 4:
    DragonCutscene();
    break;
  case 5:
    DragonVanish();
    break;
  case 6:
    DragonCount();
    break;
  case DRAGON_END:
    DragonEnd();
    break;
  }
  g_UpdateParticle(g_DeltaTime);
  if (dc.m_State > 0)
    LoadDragonCutscene();
  /* The crystal's pieces still flying. */
  for (m = g_LevelMobys; m->m_State != 0xFF; m++)
    if (m->m_Class == 0xFB && m->m_State < 0x80)
      MobyUpdateDragonFragment(m);
}

void func_8002F3E4(void) {
  GS_SHADOW_CALL(func_8002F3E4, Dragon);
  Dragon();
}

#undef dc
#endif
