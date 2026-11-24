/* enc_co_* (core/include/encore/platform.h) on Windows, over fibers: what core/src/enc_co.c does with <ucontext.h>
 * elsewhere. The thread that resumes a coroutine becomes a fiber the first time (ConvertThreadToFiber) and stays
 * one. */
#include "encore/platform.h"

#include <stdlib.h>
#include <windows.h>

#define ENC_CO_MIN_STACK (64u << 10)

struct EncCo {
  void *fiber, *caller;
  void (*fn)(void *);
  void *arg;
  int done;
};

static __thread EncCo *running;

static void WINAPI trampoline(void *p) {
  EncCo *co = (EncCo *)p;
  co->fn(co->arg);
  co->done = 1;
  SwitchToFiber(co->caller); /* never resumed again */
}

int enc_co_create(EncCo **out, void (*fn)(void *arg), void *arg, size_t stack) {
  EncCo *co;
  if (!out || !fn) return ENC_ERR_ARG;
  *out = NULL;
  if (stack < ENC_CO_MIN_STACK) stack = ENC_CO_MIN_STACK;
  co = (EncCo *)calloc(1, sizeof *co);
  if (!co) return ENC_ERR_NOMEM;
  co->fn = fn, co->arg = arg;
  co->fiber = CreateFiber(stack, trampoline, co);
  if (!co->fiber) {
    free(co);
    return ENC_ERR_SYSTEM;
  }
  *out = co;
  return ENC_OK;
}

int enc_co_resume(EncCo *co) {
  EncCo *prev;
  void *self;
  if (!co) return ENC_ERR_ARG;
  if (co->done) return 0;
  self = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
  if (!self) return ENC_ERR_SYSTEM;
  prev = running;
  running = co;
  co->caller = self;
  SwitchToFiber(co->fiber);
  running = prev;
  return co->done ? 0 : 1;
}

void enc_co_yield(void) {
  EncCo *co = running;
  if (co) SwitchToFiber(co->caller);
}

EncCo *enc_co_self(void) { return running; }

void enc_co_destroy(EncCo *co) {
  if (!co) return;
  DeleteFiber(co->fiber);
  free(co);
}
