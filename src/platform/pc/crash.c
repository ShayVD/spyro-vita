/* A crash's report on PC (docs/pc-build.md): on a fault the faulting
 * address, the instruction pointer and a backtrace go to stderr and to
 * spyro.log, so a player's log says where (llvm-symbolizer --obj=spyro
 * ADDR names the function: the executable is non-PIE, its addresses are its
 * own). Linux only; Windows reports through its own error reporting. */
#if defined(__linux__)
#define _GNU_SOURCE 1
#include "pc.h"
#include "host.h"

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static char s_alt[65536];

static void on_fault(int sig, siginfo_t *si, void *uc_) {
  ucontext_t *uc = uc_;
  void *bt[48];
  char line[256];
  int n = backtrace(bt, 48), len;
  len = snprintf(line, sizeof line, "spyro: signal %d at address %p, rip 0x%llx, rsp 0x%llx\n", sig,
                 si->si_addr, (unsigned long long)uc->uc_mcontext.gregs[REG_RIP],
                 (unsigned long long)uc->uc_mcontext.gregs[REG_RSP]);
  if (write(2, line, (size_t)len) < 0)
    (void)0;
  backtrace_symbols_fd(bt, n, 2);
  host_log("crash: signal %d at address %p, rip 0x%llx", sig, si->si_addr,
           (unsigned long long)uc->uc_mcontext.gregs[REG_RIP]);
  for (int i = 0; i < n; i++)
    host_log("crash: #%d %p", i, bt[i]);
  host_log_flush();
  signal(sig, SIG_DFL);
  raise(sig);
}

void pc_crash_init(void) {
  stack_t ss = {.ss_sp = s_alt, .ss_size = sizeof s_alt};
  struct sigaction sa;
  sigaltstack(&ss, NULL);
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_fault;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
}
#else
void pc_crash_init(void) {}
#endif
