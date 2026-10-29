/* asm/system_time.s by hand: func_80017FD4 reads root counter 2
 * (0x1F801120, system clock / 8 = 4,233,600 Hz), used once as a seed in
 * titlescreen.c. The port derives it from the virtual vblank count so runs
 * stay deterministic. */
extern int VSync(int mode);

int func_80017FD4(void) {
  return (int)((unsigned)VSync(-1) * 70630u & 0xFFFFu); /* ticks per NTSC vblank */
}
