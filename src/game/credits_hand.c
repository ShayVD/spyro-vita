/* The credits overlay (src/overlays/credits.c, asm/nonmatchings/overlays/
 * credits): its update (func_credits_8007AA50: the camera's path, the
 * letters flying in and out, the music's fade and the next part's load)
 * and its draw (func_credits_8007BFD0), written by hand from the asm so
 * they read as C (docs/native-plan.md stage 3, decisions D94). Compiled C
 * in the original, so this is decompiling: the asm's offsets are checked
 * by static_asserts. They replace the mips2c units (cmake/mips2c.cmake
 * M2C_HAND); checked against the units with the shadow of hand_shadow.h
 * (in a child: they read the CD, play sounds and draw) and against
 * PCSX-Redux with the oracle replay (docs/translation-log.md).
 * M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) compiles one out for its unit. */
#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdlib.h>

#include "common.h"

#include "buffers.h"
#include "camera.h"
#include "cd.h"
#include "cutscene.h"
#include "cyclorama.h"
#include "environment.h"
#include "gamepad.h"
#include "gamestates/draw.h"
#include "graphics.h"
#include "hand_shadow.h"
#include "libspu.h"
#include "loaders.h"
#include "memory.h"
#include "moby.h"
#include "moby_helpers.h"
#include "rand.h"
#include "renderers.h"
#include "sony_image.h"
#include "specular_and_metal.h"
#include "spu.h"
#include "wad.h"

#include <libcd.h>
#include <libetc.h>
#include <libgpu.h>
#include <stddef.h>
#include <stdint.h>

/* A letter of the credits: a HUD moby spelling a line, flying in, waiting,
 * then falling away. 100 of them at the overlay's end (D_8007589C),
 * D_800756E4 in use. */
typedef struct {
  u_char unk_0x0[4];
  u_char m_State;    /* LETTER_* */
  u_char m_Angle[3]; /* x, y, z, the moby's rotation */
  u_char m_Spin[3];  /* added to m_Angle each tick */
  u_char m_Timer;    /* ticks of flying in left */
  short m_Vel[3];    /* x, y, z */
  short m_Pos[3];    /* x, y, z: screen x and y, and the depth */
  short m_Line;      /* the line it spells */
  short m_Model;     /* its HUD model, the character */
} CreditsLetter;
static_assert(sizeof(CreditsLetter) == 0x1C && offsetof(CreditsLetter, m_Vel) == 0xC &&
                  offsetof(CreditsLetter, m_Pos) == 0x12 && offsetof(CreditsLetter, m_Line) == 0x18,
              "CreditsLetter");
enum { LETTER_FREE, LETTER_IN, LETTER_PLACED, LETTER_OUT, LETTER_GONE };
#define LETTER_SLOTS 100

/* A line of the credits (D_80075708, from the part's data). */
typedef struct {
  char *m_Text;
  short m_Y;
  short m_ShowTimer; /* until it leaves */
  short m_NextTimer; /* until the next line is spelt */
  short m_State;     /* LINE_* */
} CreditsLine;
static_assert(sizeof(CreditsLine) == 0xC && offsetof(CreditsLine, m_ShowTimer) == 6, "CreditsLine");
enum { LINE_WAITING, LINE_SPELT, LINE_LEAVING };

/* A part's camera path (g_CreditsDataPtr): ticks so far, the lines' data
 * (from g_Buffers.m_LevelScene), the path's length, then a key per two
 * ticks. */
typedef struct {
  int m_Ticks;
  int m_LinesOffset;
  int m_Keys;
  struct {
    Vector3D m_Position;
    int m_Rotation[3];
  } m_Key[1];
} CreditsPath;
static_assert(offsetof(CreditsPath, m_Key) == 0xC && sizeof(((CreditsPath *)0)->m_Key[0]) == 0x18, "CreditsPath");

static_assert(offsetof(Moby, m_Rotation) == 0x44 && offsetof(Moby, m_DepthOffset) == 0x47, "Moby");
static_assert(offsetof(Moby, m_SpecularMetalType) == 0x4F && offsetof(Moby, m_RenderRadius) == 0x50, "Moby");
static_assert(sizeof(DB) == 0x84 && offsetof(DB, m_DispEnv) == 0x5C && offsetof(DRAWENV, r0) == 0x19, "DB");
static_assert(offsetof(__typeof__(g_Spu), m_SoundDefinitions) == 0x2C8 &&
                  offsetof(__typeof__(g_Spu), m_NextSoundOverrideFlags) == 0x2D4 &&
                  offsetof(__typeof__(g_Spu), m_VolumeOverride) == 0x2E0,
              "g_Spu");
static_assert(offsetof(__typeof__(g_WadHeader), m_Credits1Data) == 0x290, "g_WadHeader");
static_assert(offsetof(CdState, m_IsReading) == 0x28, "CdState");

extern CreditsLetter *D_8007589C; /* the letters */
extern int D_800756E4;            /* letters in use */
extern CreditsLine *D_80075708;   /* the lines */
extern char *D_800757EC;          /* where the next part loads */
extern int D_80075958;            /* its data and scene's size */
extern int D_80075950[2];         /* the vblank count at the last two frames */
extern void *Terminator_credits;  /* the overlay's end (gen/ovl_table.c, D20) */

/* The mips2c units, linked as <name>_gen with SPYRO_M2C_REF or
 * SPYRO_M2C_FUNCS. */
u_int func_credits_8007AA50_gen(), func_credits_8007BFD0_gen();

/* The parts' places in WAD.WAD: ten for each ending's credits. */
#define PART_OFFSET (((OffsetLength *)g_WadHeader.m_Credits1Data)[g_CreditsSequence].m_Offset)
#define CD_TIME 600

#if !M2C_func_credits_8007AA50

/* The first part, loaded while the screen is still: its header, the
 * textures (two halves), the sounds into SPU RAM, the data and the scene,
 * then the music and the lines. */
static void FirstPart(void) {
  RECT rect;
  char *blob, *text;
  int i;
  if (!g_CreditsBuffer) {
    char *end = Terminator_credits;
    D_8007589C = (CreditsLetter *)end;
    D_800756E4 = 0;
    for (i = 0; i < LETTER_SLOTS; i++)
      D_8007589C[i].m_State = LETTER_FREE;
    D_80075708 = (CreditsLine *)(end + 0xAF0);
    g_CreditsBuffer = (int)(uintptr_t)(end + 0x2800);
    g_Buffers.m_CopyBuf = g_Buffers.m_DiscCopyBuf = (void *)(uintptr_t)g_CreditsBuffer;
  }
  CDLoadSync(g_CdState.m_WadSector, g_Buffers.m_DiscCopyBuf, 0x800, PART_OFFSET, CD_TIME);
  Memcpy(&g_LevelHeader, g_Buffers.m_DiscCopyBuf, 0x1D0);
  CDLoadSync(g_CdState.m_WadSector, g_Buffers.m_DiscCopyBuf, 0x60000, g_LevelHeader.m_VramSramOffset + PART_OFFSET,
             CD_TIME);
  setRECT(&rect, 0x200, 0, 0x200, 0x180);
  LoadImage(&rect, g_Buffers.m_DiscCopyBuf);
  CDLoadSync(g_CdState.m_WadSector, g_Buffers.m_DiscCopyBuf, g_LevelHeader.m_VramSramSize - 0x60000,
             g_LevelHeader.m_VramSramOffset + PART_OFFSET + 0x60000, CD_TIME);
  SpuSetTransferStartAddr(0x1010);
  SpuWrite(g_Buffers.m_DiscCopyBuf, 0x7EFF0);
  while (!SpuIsTransferCompleted(SPU_TRANSFER_PEEK))
    ;
  CDLoadSync(g_CdState.m_WadSector, g_Buffers.m_DiscCopyBuf, g_LevelHeader.m_DataSize,
             g_LevelHeader.m_DataOffset + PART_OFFSET, CD_TIME);
  g_Buffers.m_ModelData = LoadLevelData(g_Buffers.m_DiscCopyBuf, 1);
  g_Buffers.m_LevelScene = g_Buffers.m_ModelData;
  g_Cyclorama = g_NewCyclorama;
  g_Buffers.m_LevelSceneOffset = g_LevelHeader.m_SceneOffset;
  g_Buffers.m_LevelSceneSize = g_LevelHeader.m_SceneSize;
  g_PortalCount = 0;
  CDLoadSync(g_CdState.m_WadSector, g_Buffers.m_LevelScene, g_Buffers.m_LevelSceneSize,
             g_LevelHeader.m_SceneOffset + PART_OFFSET, CD_TIME);
  /* The music: one sound at 0x1010 in SPU RAM, full volume. */
  g_Spu.m_SoundDefinitions = &g_CutsceneSoundDef;
  g_Spu.m_SoundDefinitions->m_Addr = 0x1010;
  g_Spu.m_SoundDefinitions->m_LoopAddr = -1;
  g_Spu.m_SoundDefinitions->unk_0x8 = 0x50;
  g_Spu.m_SoundDefinitions->m_Pitch = 0x659;
  g_Spu.m_SoundDefinitions->m_PitchVariance = 0;
  g_Spu.m_SoundDefinitions->m_PitchMultiplier = 0;
  g_CreditsDataPtr = (int)(uintptr_t)g_Buffers.m_LevelScene;
  g_Spu.m_SoundDefinitions->m_VarianceType = 0;
  g_Spu.m_NextSoundOverrideFlags = 1;
  g_Spu.m_VolumeOverride.left = g_Spu.m_VolumeOverride.right = 0x3FFF;
  PlaySound(0, NULL, 0x10, NULL);
  /* The lines, with their text pointers made absolute. */
  blob = (char *)g_Buffers.m_LevelScene + ((CreditsPath *)(uintptr_t)g_CreditsDataPtr)->m_LinesOffset;
  D_800757EC = blob;
  g_CreditsTotalEntries = ((int *)blob)[2];
  text = (char *)D_80075708 + ((int *)blob)[1] - 0xC;
  Memcpy(D_80075708, blob + 0xC, ((int *)blob)[0] - 0xC);
  for (i = 0; i < g_CreditsTotalEntries; i++)
    D_80075708[i].m_Text += (uintptr_t)text;
  g_CreditsStage = 0;
  g_CreditsSequence++;
}

/* The camera along the part's path, a key per two ticks, with the fade
 * in over its first 32 ticks and out over its last 32; at its end, the
 * next stage. */
static void CameraPath(void) {
  CreditsPath *path = (CreditsPath *)(uintptr_t)g_CreditsDataPtr;
  int t, end, edge, k;
  if (!path || g_CreditsStage < 0)
    return;
  t = path->m_Ticks += g_DeltaTime;
  end = path->m_Keys * 2;
  if (t >= end) {
    g_CreditsStage++;
    return;
  }
  edge = t < 0x20 ? t : end - t;
  g_Fade = edge < 0x20 ? 16 - (edge >> 1) : 0;
  if (g_Fade >= 16)
    g_Fade = 15;
  if (g_Fade < 0)
    g_Fade = 0;
  k = path->m_Ticks >> 1;
  g_Camera.m_Position = path->m_Key[k].m_Position;
  g_Camera.m_Rotation.x = path->m_Key[k].m_Rotation[0];
  g_Camera.m_Rotation.y = path->m_Key[k].m_Rotation[1];
  g_Camera.m_Rotation.z = path->m_Key[k].m_Rotation[2];
}

/* After the last line, with no letter left: the music fades out, the
 * screen to black, then stage 99. */
static void FadeOut(void) {
  if (D_800756E4 || g_CreditsEntryIndex < g_CreditsTotalEntries)
    return;
  g_Spu.m_SoundDefinitions->unk_0x8 -= 10;
  g_CreditsTimer += g_DeltaTime;
  if (g_Spu.m_SoundDefinitions->unk_0x8 < 0)
    g_Spu.m_SoundDefinitions->unk_0x8 = 0;
  g_Fade = g_CreditsTimer;
  if (g_Fade >= 16)
    g_Fade = 15;
  if (g_Fade == 15)
    g_CreditsStage = 99;
}

/* A letter slot for a line: the first free one from *from on (*from moves
 * to it), or a new one at the end. */
static CreditsLetter *TakeLetter(int *from) {
  int i;
  for (i = *from; i < D_800756E4; i++)
    if (D_8007589C[i].m_State == LETTER_FREE) {
      *from = i;
      return &D_8007589C[i];
    }
  return &D_8007589C[D_800756E4++];
}

static inline int IsDigit(int c) { return (u_int)(c - '0') < 10; }
static inline int IsUpper(int c) { return (u_int)(c - 'A') < 26; }
static inline int IsLower(int c) { return (u_int)(c - 'a') < 26; }

/* The layout of a line being spelt: the pen's x, the line's y and the
 * depth, the gap after a lower-case letter, a lower-case letter's drop
 * and depth. */
typedef struct {
  int x, y, z, gap, drop, lowerZ;
} Pen;

/* Line s's letters, from the middle of the screen, then centred, each
 * set to fly in over 30 ticks from off to the side and behind, spinning.
 * big: the last letter was a capital or a digit (or a space), which sets
 * the next one's gap; it carries over from line to line. */
static void SpellLine(int s, int *big) {
  CreditsLine *line = &D_80075708[s];
  const char *c;
  int from = 0, colon = 0, half, i;
  Pen pen;
  line->m_State = LINE_SPELT;
  if (g_CreditsDisplayedCount < s)
    g_CreditsDisplayedCount = s;
  if (g_CreditsTotalEntries < g_CreditsDisplayedCount)
    g_CreditsDisplayedCount = g_CreditsTotalEntries;
  pen.x = 0x100, pen.y = line->m_Y, pen.z = 0x12B3, pen.gap = 0xE, pen.drop = 1, pen.lowerZ = 0x1600;
  for (c = line->m_Text; *c; c++) {
    CreditsLetter *l;
    if (*c == ' ') {
      pen.x += pen.gap * 3 / 4;
      *big = 1;
      continue;
    }
  colon:
    l = TakeLetter(&from);
    if (IsUpper((u_char)*c))
      *big = 1;
    if (IsLower((u_char)*c))
      *big = 0;
    l->m_State = LETTER_IN;
    l->m_Line = s;
    if (D_800756E4 > LETTER_SLOTS)
      exit(0);
    l->m_Pos[0] = pen.x;
    l->m_Pos[1] = pen.y;
    l->m_Pos[2] = pen.z;
    if (!*big) {
      l->m_Pos[1] += pen.drop;
      l->m_Pos[2] = pen.lowerZ;
    }
    /* The HUD's font models: digits, capitals (lower case uses them
     * too), and five signs; anything else is a raised comma. A colon is
     * two dots, the second 6 higher, at one place. */
    if (IsDigit((u_char)*c))
      l->m_Model = (u_char)*c + 0xD4;
    else if (IsUpper((u_char)*c))
      l->m_Model = (u_char)*c + 0x169;
    else if (IsLower((u_char)*c))
      l->m_Model = (u_char)*c + 0x149;
    else if (*c == '!')
      l->m_Model = 0x4B;
    else if (*c == ',')
      l->m_Model = 0x4C;
    else if (*c == '.')
      l->m_Model = 0x147;
    else if (*c == '-')
      l->m_Model = 0x115;
    else if (*c == ':') {
      l->m_Model = 0x147;
      l->m_Pos[2] += 0x578;
      if (!colon) {
        colon = 1;
        goto colon;
      }
      colon = 0;
      l->m_Pos[1] -= 6;
    } else {
      l->m_Model = 0x4C;
      l->m_Pos[1] -= pen.gap * 2 / 3;
    }
    pen.x += *big ? 0x10 : pen.gap;
    *big = IsDigit((u_char)*c);
  }
  /* Centred on the screen, then sent back 30 ticks along their flight. */
  half = (pen.x - 0x100) >> 1;
  for (i = 0; i < D_800756E4; i++) {
    CreditsLetter *l = &D_8007589C[i];
    int x;
    if (l->m_Line != s)
      continue;
    x = (short)(l->m_Pos[0] - half);
    l->m_Vel[0] = -((x - 0x100) * 3) / 30;
    l->m_Vel[2] = -(l->m_Pos[2] * 2) / 30;
    l->m_Pos[0] = x - l->m_Vel[0] * 30;
    l->m_Pos[2] -= l->m_Vel[2] * 30;
    l->m_Spin[0] = RandRangeSigned(4, 15);
    l->m_Spin[1] = RandRangeSigned(4, 15);
    l->m_Spin[2] = RandRangeSigned(4, 15);
    l->m_Angle[0] = -l->m_Spin[0] * 30;
    l->m_Angle[1] = -l->m_Spin[1] * 30;
    l->m_Angle[2] = -l->m_Spin[2] * 30;
    l->m_Timer = 30;
  }
}

/* The lines from the current one to the last shown: each one's timers;
 * the first line is spelt first, then each next one when the one before's
 * second timer runs out. */
static void Lines(void) {
  int last = g_CreditsDisplayedCount, s, big = 1, spell;
  SpecularUpdate(3);
  for (s = g_CreditsEntryIndex; s <= last; s++) {
    CreditsLine *line = &D_80075708[s];
    spell = -1;
    if (func_80037F90(&line->m_ShowTimer, sizeof(short))) {
      line->m_State = LINE_LEAVING;
      if (!line->m_NextTimer && s == g_CreditsEntryIndex)
        g_CreditsEntryIndex = s + 1;
    }
    /* Before anything is spelt, the first line. */
    if (D_80075708[0].m_State == LINE_WAITING)
      spell = 0;
    else if (func_80037F90(&line->m_NextTimer, sizeof(short)) && line[1].m_State == LINE_WAITING &&
             s < g_CreditsTotalEntries - 1)
      spell = s + 1;
    if (spell != -1)
      SpellLine(spell, &big);
  }
}

/* The letters' flight: in to their places, then, when their line
 * leaves, out and down off the screen; the free ones at the end are
 * dropped from the count. */
static void Letters(void) {
  CreditsLetter *l;
  int i;
  for (i = 0; i < D_800756E4; i++) {
    l = &D_8007589C[i];
    if (D_80075708[l->m_Line].m_State != LINE_LEAVING)
      continue;
    if (l->m_State == LETTER_PLACED)
      l->m_State = LETTER_OUT;
    else if (l->m_State == LETTER_GONE)
      l->m_State = LETTER_FREE;
  }
  for (i = 0; i < D_800756E4; i++) {
    l = &D_8007589C[i];
    if (l->m_State == LETTER_IN) {
      l->m_Pos[0] += l->m_Vel[0];
      l->m_Angle[0] += l->m_Spin[0];
      l->m_Pos[2] += l->m_Vel[2];
      l->m_Angle[1] += l->m_Spin[1];
      l->m_Angle[2] += l->m_Spin[2];
      if (--l->m_Timer)
        continue;
      /* In place: the fall it will take later. */
      l->m_State = LETTER_PLACED;
      l->m_Vel[0] = RandRangeSigned(3, 11);
      l->m_Vel[1] = -RandRange(6, 10);
      l->m_Vel[2] = RandRange(-35, 35);
      l->m_Spin[0] = RandRangeSigned(4, 9);
      l->m_Spin[1] = RandRangeSigned(4, 9);
      l->m_Spin[2] = RandRangeSigned(4, 9);
    } else if (l->m_State == LETTER_OUT) {
      l->m_Pos[0] += l->m_Vel[0];
      l->m_Pos[1] += l->m_Vel[1];
      l->m_Pos[2] += l->m_Vel[2];
      l->m_Angle[0] += l->m_Spin[0];
      l->m_Vel[1]++;
      l->m_Angle[1] += l->m_Spin[1];
      l->m_Angle[2] += l->m_Spin[2];
      if (l->m_Vel[1] >= 0x15)
        l->m_Vel[1] = 0x14;
      if (l->m_Pos[1] >= 0x119)
        l->m_State = LETTER_GONE;
    }
  }
  while (D_800756E4 > 0 && D_8007589C[D_800756E4 - 1].m_State == LETTER_FREE)
    D_800756E4--;
}

/* The next part, loaded in the background while this one plays: the
 * header (0), the first textures (1), the rest of the textures with the
 * data and scene (2), then (stage 3, set by the last part) nothing more,
 * or (4) the textures into place and the data set up. Each stage starts
 * once the CD has finished the one before. */
static void NextPart(void) {
  RECT rect;
  if (g_CreditsStage == -1)
    return;
  if (g_CreditsStage > 0) {
    CDLoadTime();
    if (g_CdState.m_IsReading || CdSync(1, NULL) != CdlComplete)
      return;
  }
  switch (g_CreditsStage) {
  case 0:
    CDLoadAsync(g_CdState.m_WadSector, D_800757EC, 0x800, PART_OFFSET, CD_TIME);
    g_CreditsStage++;
    break;
  case 1:
    Memcpy(&g_LevelHeader, D_800757EC, 0x1D0);
    CDLoadAsync(g_CdState.m_WadSector, D_800757EC, 0x20000, g_LevelHeader.m_VramSramOffset + PART_OFFSET, CD_TIME);
    g_CreditsStage++;
    break;
  case 2:
    setRECT(&rect, 0x200, 0x180, 0x200, 0x80);
    DrawSync(0);
    LoadImage(&rect, (u_long *)D_800757EC);
    DrawSync(0);
    D_80075958 = g_LevelHeader.m_DataSize + g_LevelHeader.m_SceneSize + 0x40000;
    CDLoadAsync(g_CdState.m_WadSector, D_800757EC, D_80075958, g_LevelHeader.m_VramSramOffset + PART_OFFSET + 0x20000,
                CD_TIME);
    g_CreditsStage++;
    break;
  case 3:
    D_8007576C = 0;
    break;
  case 4:
    setRECT(&rect, 0x200, 0x80, 0x200, 0x100);
    g_Buffers.m_CopyBuf = g_Buffers.m_DiscCopyBuf = (void *)(uintptr_t)g_CreditsBuffer;
    DrawSync(0);
    LoadImage(&rect, (u_long *)D_800757EC);
    setRECT(&rect, 0x200, 0x180, 0x200, 0x80);
    MoveImage(&rect, 0x200, 0);
    DrawSync(0);
    Memcpy((void *)(uintptr_t)g_CreditsBuffer, D_800757EC + 0x40000, D_80075958 - 0x40000);
    D_800757EC = (char *)(uintptr_t)g_CreditsBuffer + D_80075958 - 0x40000;
    g_Buffers.m_ModelData = LoadLevelData(g_Buffers.m_DiscCopyBuf, 1);
    g_Cyclorama = g_NewCyclorama;
    g_Buffers.m_LevelScene = (char *)g_Buffers.m_DiscCopyBuf + g_LevelHeader.m_DataSize;
    g_CreditsDataPtr = (int)(uintptr_t)g_Buffers.m_LevelScene;
    g_Buffers.m_LevelSceneSize = g_LevelHeader.m_SceneSize;
    g_Buffers.m_LevelSceneOffset = g_LevelHeader.m_SceneOffset;
    g_CreditsStage = g_CreditsSequence % 10 < 9 ? 0 : 3;
    g_CreditsSequence++;
    break;
  }
}

static void Update(void) {
  if (g_CreditsStage == -1)
    FirstPart();
  CameraPath();
  FadeOut();
  if (D_800756E4 > 0 || (g_CreditsTotalEntries > 0 && g_CreditsEntryIndex < g_CreditsTotalEntries)) {
    Lines();
    Letters();
  }
  NextPart();
}

/* The credits' update, each tick. */
void func_credits_8007AA50(void) {
  HAND_SHADOW_FORK(func_credits_8007AA50, 0, 0, Update());
  Update();
}
#endif

#if !M2C_func_credits_8007BFD0

/* The letters as HUD mobys (taken below g_HudMobys), with the scene and
 * the sky behind; or, before the first part, the last frame copied into
 * the other buffer. Then the frame, at most one per two vblanks. */
static void Draw(void) {
  int i, y;
  RECT rect;
  if (g_CreditsStage == -1) {
    DrawSync(0);
    VSync(0);
    y = g_CurDB == &g_DB[0] ? 8 : 0xF8;
    setRECT(&rect, 0, y, 0x200, 0xE0);
    MoveImage(&rect, 0, 0x100 - y);
    DrawSync(0);
    return;
  }
  g_DB[0].m_DrawEnv.r0 = g_DB[1].m_DrawEnv.r0 = g_Cyclorama.m_BackgroundColor.r;
  g_DB[0].m_DrawEnv.g0 = g_DB[1].m_DrawEnv.g0 = g_Cyclorama.m_BackgroundColor.g;
  g_DB[0].m_DrawEnv.b0 = g_DB[1].m_DrawEnv.b0 = g_Cyclorama.m_BackgroundColor.b;
  for (i = 0; i < D_800756E4; i++) {
    const CreditsLetter *l = &D_8007589C[i];
    Moby *m = --g_HudMobys;
    Memset(m, 0, sizeof *m);
    m->m_Position.x = l->m_Pos[0];
    m->m_Position.y = l->m_Pos[1];
    m->m_Position.z = l->m_Pos[2];
    m->m_Rotation.x = l->m_Angle[0];
    if (l->m_Model == 0x115) {
      /* The dash, turned flat and nearer. */
      m->m_Rotation.x += 0x29;
      m->m_Position.z = 0x1C00;
    }
    m->m_Rotation.y = l->m_Angle[1];
    m->m_Rotation.z = l->m_Angle[2];
    m->m_Class = l->m_Model;
    m->m_SpecularMetalType = 0xB;
    m->m_RenderRadius = 0xFF;
    m->m_DepthOffset = 0x7F;
  }
  if (g_Fade)
    func_800190D4(2, g_Fade << 4, g_Fade << 4, g_Fade << 4);
  g_SonyImage.m_ShadedMobys[0] = NULL; /* D_8006FCF4+0x2400, the lit list */
  func_80018880();
  Memset(&g_SonyImage, 0, 0x900);
  func_80022A2C();
  Memset16(&g_SonyImage, 0, 0x1C00);
  g_Environment.m_CullingDistance = 0x28000;
  func_800258F0(-1);
  func_8004EBA8(-1, &g_Camera.m_ViewMatrix, &g_Camera.m_ProjectionMatrix);
  DrawSync(0);
  if (D_80075784)
    VSync(0);
  for (D_80075950[1] = VSync(-1); D_80075950[1] - D_80075950[0] < 2; D_80075950[1] = VSync(-1))
    VSync(0);
  D_80075950[0] = VSync(-1);
  PutDispEnv(&g_CurDB->m_DispEnv);
  PutDrawEnv(&g_CurDB->m_DrawEnv);
  DrawOTag(func_80016784(0x800));
}

/* The credits' draw, each frame. */
void func_credits_8007BFD0(void) {
  HAND_SHADOW_FORK(func_credits_8007BFD0, 0, 0, Draw());
  Draw();
}
#endif
