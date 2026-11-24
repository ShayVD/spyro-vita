/* enc_co_* (core/include/encore/platform.h) over <ucontext.h>: the host's libc on Linux and macOS, and on newlib
 * targets (the Vita) targets/vita/runtime/ucontext's std/ucontext.h with vpk_ucontext.S. A coroutine runs on the
 * thread that resumes it; each thread has its own "running" pointer (ENC_TLS: define it empty for a target without
 * thread-local storage, and then resume coroutines from one thread only). */
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600 /* macOS hides the ucontext calls without it */
#endif
#include "encore/platform.h"

#include <stdlib.h>
#include <ucontext.h>

#if defined(__APPLE__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#ifndef ENC_TLS
#define ENC_TLS __thread
#endif

#define ENC_CO_MIN_STACK (64u << 10)

struct EncCo {
  ucontext_t ctx, caller;
  void (*fn)(void *);
  void *arg;
  void *stack;
  int done;
};

static ENC_TLS EncCo *running;

static void trampoline(void) {
  EncCo *co = running;
  co->fn(co->arg);
  co->done = 1;
  /* back to whoever resumed us; never resumed again */
  swapcontext(&co->ctx, &co->caller);
}

int enc_co_create(EncCo **out, void (*fn)(void *arg), void *arg, size_t stack) {
  EncCo *co;
  if (!out || !fn) return ENC_ERR_ARG;
  *out = NULL;
  if (stack < ENC_CO_MIN_STACK) stack = ENC_CO_MIN_STACK;
  co = (EncCo *)calloc(1, sizeof *co);
  if (!co) return ENC_ERR_NOMEM;
  co->stack = malloc(stack);
  if (!co->stack) {
    free(co);
    return ENC_ERR_NOMEM;
  }
  if (getcontext(&co->ctx) != 0) {
    free(co->stack);
    free(co);
    return ENC_ERR_SYSTEM;
  }
  co->ctx.uc_stack.ss_sp = co->stack;
  co->ctx.uc_stack.ss_size = stack;
  co->ctx.uc_link = NULL;
  makecontext(&co->ctx, trampoline, 0);
  co->fn = fn, co->arg = arg;
  *out = co;
  return ENC_OK;
}

int enc_co_resume(EncCo *co) {
  EncCo *prev;
  if (!co) return ENC_ERR_ARG;
  if (co->done) return 0;
  prev = running;
  running = co;
  swapcontext(&co->caller, &co->ctx);
  running = prev;
  return co->done ? 0 : 1;
}

void enc_co_yield(void) {
  EncCo *co = running;
  if (co) swapcontext(&co->ctx, &co->caller);
}

EncCo *enc_co_self(void) { return running; }

void enc_co_destroy(EncCo *co) {
  if (!co) return;
  free(co->stack);
  free(co);
}
