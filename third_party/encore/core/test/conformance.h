#ifndef ENC_CONFORMANCE_H
#define ENC_CONFORMANCE_H
#include "encore/platform.h"
/* Runs every portable check against the linked backend, after enc_platform_init: the number of failures. */
int enc_conformance_run(void);
#endif
