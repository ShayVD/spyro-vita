/* PsyCross's GTE (PsyX_GTE.cpp) against PCSX-Redux, command by command
 * (docs/porting-plan.md §9 step 8, decision D2).
 * usage: test_gte <gte.bin> [-v]
 * gte.bin comes from tools/oracle/oracle.lua (ORACLE_GTE): per record 131
 * u32, the address, the instruction word, the vblank, then data[32]
 * control[32] before the command and data[32] control[32] after it, as raw
 * register storage. Both emulators descend from PCSX and store the registers
 * the same way, so the "before" state is loaded raw; the results are compared
 * as the CPU would read them (MFC2/CFC2), which hides storage-only
 * differences such as the unused upper half of a 16-bit register.
 * Exit 77 (skipped) when the vectors file is missing. */
#include <gtereg.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t addr, word, vblank;
  uint32_t before[64], after[64];
} Record;

static const char *op_name(unsigned op) {
  switch (op) {
  case 0x01: return "RTPS";
  case 0x06: return "NCLIP";
  case 0x0C: return "OP";
  case 0x10: return "DPCS";
  case 0x11: return "INTPL";
  case 0x12: return "MVMVA";
  case 0x13: return "NCDS";
  case 0x14: return "CDP";
  case 0x16: return "NCDT";
  case 0x1B: return "NCCS";
  case 0x1C: return "CC";
  case 0x1E: return "NCS";
  case 0x20: return "NCT";
  case 0x28: return "SQR";
  case 0x29: return "DCPL";
  case 0x2A: return "DPCT";
  case 0x2D: return "AVSZ3";
  case 0x2E: return "AVSZ4";
  case 0x30: return "RTPT";
  case 0x3D: return "GPF";
  case 0x3E: return "GPL";
  case 0x3F: return "NCCT";
  }
  return "?";
}

static uint32_t clamp5(int32_t ir) {
  ir >>= 7;
  return ir < 0 ? 0 : ir > 0x1F ? 0x1F : (uint32_t)ir;
}

static uint32_t lzc(uint32_t v) {
  uint32_t n = 0;
  if (v & 0x80000000u)
    v = ~v;
  while (n < 32 && !(v & 0x80000000u)) {
    v <<= 1;
    n++;
  }
  return n;
}

/* Register i (0-31 data, 32-63 control) as MFC2/CFC2 return it
 * (psx-spx "GTE Registers"), from raw storage r. */
static uint32_t cpu_view(const uint32_t *r, int i) {
  switch (i) {
  case 1: case 3: case 5: case 8: case 9: case 10: case 11:
    return (uint32_t)(int32_t)(int16_t)r[i];
  case 7: case 16: case 17: case 18: case 19:
    return r[i] & 0xFFFF;
  case 15:
    return r[14];
  case 28: case 29:
    return clamp5((int16_t)r[9]) | clamp5((int16_t)r[10]) << 5 |
           clamp5((int16_t)r[11]) << 10;
  case 31:
    return lzc(r[30]);
  case 32 + 4: case 32 + 12: case 32 + 20: case 32 + 26: case 32 + 27:
  case 32 + 29: case 32 + 30:
    return (uint32_t)(int32_t)(int16_t)r[i];
  }
  return r[i];
}

static const char *reg_name(int i) {
  static const char *d[32] = {
      "VXY0", "VZ0",  "VXY1", "VZ1",  "VXY2", "VZ2",  "RGBC", "OTZ",
      "IR0",  "IR1",  "IR2",  "IR3",  "SXY0", "SXY1", "SXY2", "SXYP",
      "SZ0",  "SZ1",  "SZ2",  "SZ3",  "RGB0", "RGB1", "RGB2", "RES1",
      "MAC0", "MAC1", "MAC2", "MAC3", "IRGB", "ORGB", "LZCS", "LZCR"};
  static const char *c[32] = {
      "RT11", "RT13", "RT22", "RT31", "RT33", "TRX",  "TRY",  "TRZ",
      "L11",  "L13",  "L22",  "L31",  "L33",  "RBK",  "GBK",  "BBK",
      "LR1",  "LR3",  "LG2",  "LB1",  "LB3",  "RFC",  "GFC",  "BFC",
      "OFX",  "OFY",  "H",    "DQA",  "DQB",  "ZSF3", "ZSF4", "FLAG"};
  return i < 32 ? d[i] : c[i - 32];
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: test_gte <gte.bin> [-v]\n");
    return 2;
  }
  int verbose = argc > 2 && strcmp(argv[2], "-v") == 0;
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    printf("test_gte: no vectors at %s (skipped)\n", argv[1]);
    return 77;
  }
  static unsigned total[64], bad[64], reg_bad[64][64];
  Record rec;
  unsigned n = 0, nbad = 0;
  while (fread(&rec, sizeof rec, 1, f) == 1) {
    unsigned op = rec.word & 0x3F;
    memcpy(gteRegs.CP2D.r, rec.before, 32 * 4);
    memcpy(gteRegs.CP2C.r, rec.before + 32, 32 * 4);
    GTE_operator(rec.word & 0x1FFFFFF);
    uint32_t got[64];
    memcpy(got, gteRegs.CP2D.r, 32 * 4);
    memcpy(got + 32, gteRegs.CP2C.r, 32 * 4);
    int diff = 0;
    for (int i = 0; i < 64; i++) {
      uint32_t want = cpu_view(rec.after, i), have = cpu_view(got, i);
      if (want == have)
        continue;
      reg_bad[op][i]++;
      if (verbose && bad[op] < 5)
        printf("  %s @%08X vblank %u: %s want %08X got %08X\n", op_name(op),
               rec.addr, rec.vblank, reg_name(i), want, have);
      diff = 1;
    }
    total[op]++;
    bad[op] += diff;
    nbad += diff;
    n++;
  }
  fclose(f);
  for (unsigned op = 0; op < 64; op++) {
    if (!total[op])
      continue;
    printf("%-6s %5u vectors, %5u mismatched", op_name(op), total[op], bad[op]);
    for (int i = 0; i < 64; i++)
      if (reg_bad[op][i])
        printf(" %s:%u", reg_name(i), reg_bad[op][i]);
    printf("\n");
  }
  printf("test_gte: %u vectors, %u mismatched\n", n, nbad);
  return nbad ? 1 : 0;
}
