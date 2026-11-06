/* PsyQ's libpad for the port (stage 8a, D114), in place of PsyCross's
 * LIBPAD.C (cmake/psycross.cmake leaves it out, as it does LIBETC.C): the
 * eight entry points the game reaches, over the host's pad (psx_pad.h).
 * Each returns what LIBPAD.C returns (PsyCross, MIT; THIRD_PARTY_NOTICES.md):
 * PadInfoMode is always 7, PadSetActAlign 1, PadSetMainMode 0, and
 * PadGetState is stable or disconnected from the host's status, with
 * LIBPAD.C's slot arithmetic. The game calls PadInitDirect and PadStartCom
 * (initialization.c), PadGetState, PadInfoMode, PadSetAct, PadSetActAlign
 * and PadSetMainMode (gamepad.c), and PadStopCom (crash_demo.c, not
 * linked). The rest of libpad.h has no caller and isn't defined. */
#include "psx_pad.h"

#include <libpad.h>

int g_padCommEnable = 0;

void PadInitDirect(unsigned char *pad1, unsigned char *pad2) {
  psx_pad_init(0, pad1);
  psx_pad_init(1, pad2);
}

void PadStartCom(void) { g_padCommEnable = 1; }

void PadStopCom(void) { g_padCommEnable = 0; }

/* LIBPAD.C's slot: ((mtap * 2) + (port >> 4)) & 1, as C's precedence
 * groups its "(mtap * 2) + (port >> 4) & 1". */
static int pad_slot(int port) { return ((port & 3) * 2 + (port >> 4)) & 1; }

int PadGetState(int port) {
  return psx_pad_status(port & 3, pad_slot(port)) ? PadStateStable : PadStateDiscon;
}

int PadInfoMode(int port, int term, int offs) {
  (void)port;
  (void)term;
  (void)offs;
  return 7;
}

int PadSetActAlign(int port, unsigned char *table) {
  (void)port;
  (void)table;
  return 1;
}

int PadSetMainMode(int socket, int offs, int lock) {
  (void)socket;
  (void)offs;
  (void)lock;
  return 0;
}

void PadSetAct(int port, unsigned char *table, int len) {
  psx_pad_vibrate(port & 3, pad_slot(port), table, len);
}
