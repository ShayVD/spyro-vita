/* Synthetic decomp data for lp64/tools/test (no game's code). */
#include "game.h"

Moby *g_Player;
Moby *g_List[4] = {0};
char *g_Names[3] = {"A", "BB", "CCC"};
#ifdef PORT
int g_PortOnly;
#else
char *g_Other[2] = {"x", "y"};
#endif
const char *g_Text = "Moby *g_InString;";
