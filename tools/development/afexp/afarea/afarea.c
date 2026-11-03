/* afarea SUBDEV LEFT TOP WIDTH HEIGHT: sets the S5C73M3 autofocus area (V4L2_CID_S5C73M3_AF_AREA,
 * a struct v4l2_rect) through VIDIOC_S_EXT_CTRLS; v4l2-ctl splits the rectangle at its commas.
 * Freestanding (raw ARM EABI syscalls), no libc. Build: clang --target=armv7a-linux-gnueabihf -marm
 * -O2 -ffreestanding -fno-builtin -nostdlib -static -fuse-ld=lld -o afarea afarea.c */
struct rect { int left, top; unsigned width, height; };
struct ext_control { unsigned id, size, reserved; union { void *ptr; long long v64; }; } __attribute__((packed));
struct ext_controls { unsigned which, count, error_idx; int request_fd; unsigned reserved; struct ext_control *controls; };
static long sys(long n, long a, long b, long c, long d) {
	register long r7 asm("r7") = n, r0 asm("r0") = a, r1 asm("r1") = b, r2 asm("r2") = c, r3 asm("r3") = d;
	asm volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3) : "memory");
	return r0;
}
static unsigned dec(const char *s) { unsigned v = 0; for (; *s; s++) v = v * 10 + (*s - '0'); return v; }
__attribute__((used, noreturn)) void cmain(int argc, char **argv) {
	long rc = -22;
	if (argc == 6) {
		int fd = sys(322, -100, (long)argv[1], 2, 0); /* openat(AT_FDCWD, path, O_RDWR) */
		struct rect r = {dec(argv[2]), dec(argv[3]), dec(argv[4]), dec(argv[5])};
		struct ext_control c = {.id = 0x00981b40, .size = sizeof(r)};
		c.ptr = &r;
		struct ext_controls cs = {.count = 1, .controls = &c};
		rc = fd < 0 ? fd : sys(54, fd, 0xc0185648, (long)&cs, 0); /* VIDIOC_S_EXT_CTRLS */
	}
	if (rc < 0) sys(4, 2, (long)"afarea: failed\n", 15, 0);
	sys(1, rc < 0, 0, 0, 0);
	for (;;);
}
__attribute__((naked)) void _start(void) {
	asm("ldr r0, [sp]\n add r1, sp, #4\n bl cmain");
}
