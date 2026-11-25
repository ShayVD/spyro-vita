/* enc_event_poll / enc_event_set_handler (encore/platform.h) and the backend's side, enc_event_emit
 * (encore/backend.h): a ring of the newest events, or a handler instead. Single-threaded by the interface's rule:
 * every call comes from the thread that runs enc_input_poll. */
#include "encore/backend.h"

#include <string.h>

#define ENC_EVENT_RING 32

static struct {
  EncEvent ring[ENC_EVENT_RING];
  int head, count;
  unsigned dropped;
  void (*fn)(const EncEvent *ev, void *user);
  void *user;
} E;

void enc_event_reset(void) { memset(&E, 0, sizeof E); }

void enc_event_emit(int type, int width, int height, uint64_t suspended_us) {
  EncEvent ev;
  memset(&ev, 0, sizeof ev);
  ev.type = type, ev.width = width, ev.height = height;
  ev.time_us = enc_time_us();
  ev.suspended_us = suspended_us;
  if (E.fn) {
    E.fn(&ev, E.user);
    return;
  }
  if (E.count == ENC_EVENT_RING) { /* full: the oldest goes */
    E.head = (E.head + 1) % ENC_EVENT_RING, E.count--;
    if (E.dropped++ == 0) enc_log("encore: event queue full, dropping the oldest (nobody calls enc_event_poll?)");
  }
  E.ring[(E.head + E.count) % ENC_EVENT_RING] = ev;
  E.count++;
}

int enc_event_poll(EncEvent *ev) {
  if (!ev) return ENC_ERR_ARG;
  if (!E.count) return 0;
  *ev = E.ring[E.head];
  E.head = (E.head + 1) % ENC_EVENT_RING, E.count--;
  return 1;
}

void enc_event_set_handler(void (*fn)(const EncEvent *ev, void *user), void *user) {
  E.fn = fn;
  E.user = user;
}
