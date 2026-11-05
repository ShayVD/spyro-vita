/* tests/lean/arm_check.sh's entry: test_lean.c without a C library, run
 * under qemu-arm (Linux system calls) */
int lean_test(unsigned long long n);

void _start(void) {
  register long r0 __asm__("r0") = lean_test(1ull << 24);
  register long r7 __asm__("r7") = 1; /* exit */
  __asm__ volatile("svc 0" : : "r"(r0), "r"(r7));
  for (;;)
    ;
}
