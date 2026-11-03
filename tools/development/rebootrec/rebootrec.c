/* rebootrec [mode]: syncs and restarts through reboot(2) with a reason ("recovery" by default), past
 * Android's init, which refuses "reboot,recovery" without a /misc partition. The kernel's
 * syscon-reboot-mode tells the bootloader. Freestanding (raw ARM EABI syscalls), no libc.
 * Build: clang --target=armv7a-linux-gnueabihf -marm -O2 -ffreestanding -fno-builtin -nostdlib
 * -static -fuse-ld=lld -o rebootrec rebootrec.c */
static long sys(long n, long a, long b, long c, long d) {
	register long r7 asm("r7") = n, r0 asm("r0") = a, r1 asm("r1") = b, r2 asm("r2") = c, r3 asm("r3") = d;
	asm volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3) : "memory");
	return r0;
}
__attribute__((used, noreturn)) void cmain(int argc, char **argv) {
	sys(36, 0, 0, 0, 0); /* sync */
	/* reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, reason) */
	long rc = sys(88, 0xfee1dead, 0x28121969, 0xa1b2c3d4, (long)(argc > 1 ? argv[1] : "recovery"));
	sys(4, 2, (long)"rebootrec: failed\n", 18, 0);
	sys(1, rc < 0, 0, 0, 0);
	for (;;);
}
__attribute__((naked)) void _start(void) {
	asm("ldr r0, [sp]\n add r1, sp, #4\n bl cmain");
}
