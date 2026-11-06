/* The pad interface our libpad (libpad.c, stage 8a, D114) calls into, one
 * per host: src/platform/linux32/pad.c (the pad script) and
 * src/platform/vita/pad.c (SDL2's game controller). PsyCross called these
 * PsyX_Pad_InitPad, PsyX_Pad_GetStatus and PsyX_Pad_Vibrate. */
#ifndef PSX_PAD_H
#define PSX_PAD_H

/* PadStartCom sets it, PadStopCom clears it; the hosts fill the buffers
 * only while it's set (LIBPAD.C's flag of the same name). */
extern int g_padCommEnable;

/* PadInitDirect: slot 0 or 1 gets the game's buffer and fills it. */
void psx_pad_init(int slot, unsigned char *padData);
/* Non-zero when a pad is in that slot (PadGetState). */
int psx_pad_status(int mtap, int slot);
/* PadSetAct's actuator table (neither host vibrates). */
void psx_pad_vibrate(int mtap, int slot, unsigned char *table, int len);

#endif
