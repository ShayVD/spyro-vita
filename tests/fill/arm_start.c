/* tests/fill/arm_check.sh's entry: test_fill.c without a C library, run
 * under qemu-arm (Linux system calls) */
int fill_test(unsigned long long n);

void _start(void) {
  register long r0 __asm__("r0") = fill_test(1ull << 20);
  register long r7 __asm__("r7") = 1; /* exit */
  __asm__ volatile("svc 0" : : "r"(r0), "r"(r7));
  for (;;)
    ;
}
