/*
 * Two-stage recovery for the Galaxy Note II.
 *
 * The RECOVERY partition has 8 MiB. The kernel takes 6 of them, the LineageOS recovery ramdisk
 * would need 9 more (xz). So recovery.img carries the kernel with this loader as its ramdisk
 * (mkbootimg.mk), and the recovery ramdisk is built into the vendor partition (HIDDEN) as an
 * uncompressed cpio archive, /vendor/etc/recovery/ramdisk.cpio (Android.mk). Started by the
 * kernel as init, the loader unpacks that archive into the rootfs, as the kernel unpacks an
 * initramfs, and executes the /init it brings: from there on the recovery runs as if the
 * archive had been the ramdisk of the image.
 *
 * Without the archive (another system on the phone, a broken vendor partition) the loader
 * reboots into download mode, where Odin or Heimdall can flash a recovery.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/reboot.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define VENDOR_PARTNAME "HIDDEN"
#define VENDOR_WAIT_MS 10000
/* the loader's own nodes and mount points, gone before the recovery's init starts */
#define KMSG_NODE "/stage2.kmsg"
#define SYSFS_DIR "/stage2.sys"
#define VENDOR_NODE "/stage2.blk"
#define VENDOR_DIR "/stage2"
#define ARCHIVE VENDOR_DIR "/etc/recovery/ramdisk.cpio"

static int kmsg = -1;

static void klog(int level, const char *fmt, ...)
{
	char msg[512];
	va_list ap;
	int n;

	if (kmsg < 0)
		return;
	n = snprintf(msg, sizeof(msg), "<%d>recovery-loader: ", level);
	va_start(ap, fmt);
	n += vsnprintf(msg + n, sizeof(msg) - n, fmt, ap);
	va_end(ap);
	if (n >= (int)sizeof(msg))
		n = sizeof(msg) - 1;
	if (write(kmsg, msg, n) < 0)
		return;
}

static void __attribute__((noreturn)) fail(const char *what)
{
	klog(3, "%s: %s, rebooting into download mode\n", what, strerror(errno));
	sleep(2);
	syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2,
		"download");
	for (;;)
		pause();
}

/* The vendor partition's device number, from the partition name in sysfs */
static int find_vendor(dev_t *dev)
{
	DIR *d = opendir(SYSFS_DIR "/class/block");
	struct dirent *e;
	int found = 0;

	if (!d)
		return 0;
	while (!found && (e = readdir(d))) {
		char path[PATH_MAX], uevent[512], *line, *save;
		unsigned int major = 0, minor = 0;
		ssize_t len;
		int fd;

		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), SYSFS_DIR "/class/block/%s/uevent", e->d_name);
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		len = read(fd, uevent, sizeof(uevent) - 1);
		close(fd);
		if (len <= 0)
			continue;
		uevent[len] = '\0';
		for (line = strtok_r(uevent, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
			if (!strncmp(line, "MAJOR=", 6))
				major = strtoul(line + 6, NULL, 10);
			else if (!strncmp(line, "MINOR=", 6))
				minor = strtoul(line + 6, NULL, 10);
			else if (!strcmp(line, "PARTNAME=" VENDOR_PARTNAME))
				found = 1;
		}
		if (found)
			*dev = makedev(major, minor);
	}
	closedir(d);
	return found;
}

/* Buffered reading of the archive, which keeps track of the offset for its 4-byte alignment */
static int archive = -1;
static unsigned char buf[64 * 1024];
static size_t buf_pos, buf_len;
static unsigned long long offset;

static int fill(void)
{
	ssize_t n;

	if (buf_pos < buf_len)
		return 0;
	n = read(archive, buf, sizeof(buf));
	if (n <= 0) {
		if (!n)
			errno = ENODATA;
		return -1;
	}
	buf_pos = 0;
	buf_len = n;
	return 0;
}

/* Read n bytes to dst, or to the file fd (dst NULL, fd >= 0), or skip them (both unset) */
static int take(void *dst, int fd, size_t n)
{
	while (n) {
		size_t chunk;

		if (fill())
			return -1;
		chunk = buf_len - buf_pos;
		if (chunk > n)
			chunk = n;
		if (dst) {
			memcpy(dst, buf + buf_pos, chunk);
			dst = (char *)dst + chunk;
		} else if (fd >= 0) {
			size_t done = 0;

			while (done < chunk) {
				ssize_t w = write(fd, buf + buf_pos + done, chunk - done);

				if (w < 0)
					return -1;
				done += w;
			}
		}
		buf_pos += chunk;
		offset += chunk;
		n -= chunk;
	}
	return 0;
}

static int align4(void)
{
	return take(NULL, -1, (4 - (offset & 3)) & 3);
}

static unsigned long field(const char *hdr, int index)
{
	char hex[9];

	memcpy(hex, hdr + 6 + 8 * index, 8);
	hex[8] = '\0';
	return strtoul(hex, NULL, 16);
}

/* An absolute name or one with a ".." component */
static int unsafe(const char *name)
{
	const char *p;

	if (name[0] == '/')
		return 1;
	for (p = name; (p = strstr(p, "..")); p += 2)
		if ((p == name || p[-1] == '/') && (p[2] == '/' || !p[2]))
			return 1;
	return 0;
}

/* Unpack a newc cpio archive (mkbootfs) into the current directory; returns the entry count */
static int unpack(void)
{
	char hdr[110], name[PATH_MAX], target[PATH_MAX];
	int entries = 0;

	for (;;) {
		unsigned long mode, uid, gid, size, namesize;
		dev_t rdev;
		int fd;

		if (take(hdr, -1, sizeof(hdr)))
			return -1;
		if (memcmp(hdr, "070701", 6) && memcmp(hdr, "070702", 6)) {
			errno = EINVAL;
			return -1;
		}
		mode = field(hdr, 1);
		uid = field(hdr, 2);
		gid = field(hdr, 3);
		size = field(hdr, 6);
		rdev = makedev(field(hdr, 9), field(hdr, 10));
		namesize = field(hdr, 11);
		if (!namesize || namesize > sizeof(name)) {
			errno = ENAMETOOLONG;
			return -1;
		}
		if (take(name, -1, namesize) || align4())
			return -1;
		name[namesize - 1] = '\0';
		if (!strcmp(name, "TRAILER!!!"))
			return entries;
		if (!name[0] || !strcmp(name, ".") || unsafe(name)) {
			if (take(NULL, -1, size) || align4())
				return -1;
			continue;
		}

		switch (mode & S_IFMT) {
		case S_IFDIR:
			if (mkdir(name, mode & 07777) && errno != EEXIST)
				goto err;
			if (chown(name, uid, gid) || chmod(name, mode & 07777))
				goto err;
			break;
		case S_IFREG:
			if (unlink(name) && errno != ENOENT)
				goto err;
			fd = open(name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
			if (fd < 0)
				goto err;
			if (take(NULL, fd, size) || fchown(fd, uid, gid) || fchmod(fd, mode & 07777)) {
				close(fd);
				goto err;
			}
			close(fd);
			size = 0;
			break;
		case S_IFLNK:
			if (size >= sizeof(target)) {
				errno = ENAMETOOLONG;
				goto err;
			}
			if (take(target, -1, size))
				return -1;
			target[size] = '\0';
			size = 0;
			if (unlink(name) && errno != ENOENT)
				goto err;
			if (symlink(target, name) || lchown(name, uid, gid))
				goto err;
			break;
		case S_IFCHR:
		case S_IFBLK:
		case S_IFIFO:
		case S_IFSOCK:
			if (unlink(name) && errno != ENOENT)
				goto err;
			if (mknod(name, mode, rdev) || chown(name, uid, gid))
				goto err;
			break;
		}
		if (take(NULL, -1, size) || align4())
			return -1;
		entries++;
	}
err:
	klog(3, "unpacking %s failed\n", name);
	return -1;
}

static long elapsed_ms(const struct timespec *start)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - start->tv_sec) * 1000 + (now.tv_nsec - start->tv_nsec) / 1000000;
}

int main(int argc, char *argv[], char *envp[])
{
	struct timespec start;
	dev_t vendor;
	int waited, entries;

	(void)argc;
	if (getpid() != 1) {
		fprintf(stderr, "%s: the init of the recovery image, not to be run otherwise\n",
			argv[0]);
		return 1;
	}
	clock_gettime(CLOCK_MONOTONIC, &start);
	if (!mknod(KMSG_NODE, S_IFCHR | 0600, makedev(1, 11)))
		kmsg = open(KMSG_NODE, O_WRONLY | O_CLOEXEC);

	if (mkdir(SYSFS_DIR, 0755) || mount("sysfs", SYSFS_DIR, "sysfs", 0, NULL))
		fail("mounting sysfs");
	for (waited = 0; !find_vendor(&vendor); waited += 20) {
		if (waited >= VENDOR_WAIT_MS) {
			errno = ENODEV;
			fail("no " VENDOR_PARTNAME " partition");
		}
		usleep(20000);
	}
	umount(SYSFS_DIR);
	rmdir(SYSFS_DIR);

	if (mknod(VENDOR_NODE, S_IFBLK | 0600, vendor) || mkdir(VENDOR_DIR, 0755))
		fail("creating the vendor mount point");
	if (mount(VENDOR_NODE, VENDOR_DIR, "ext4", MS_RDONLY | MS_NOATIME | MS_NODEV | MS_NOSUID,
		  NULL))
		fail("mounting the vendor partition");
	archive = open(ARCHIVE, O_RDONLY | O_CLOEXEC);
	if (archive < 0)
		fail("opening " ARCHIVE);
	if (chdir("/") || (entries = unpack()) < 0)
		fail("unpacking the recovery ramdisk");
	close(archive);
	umount(VENDOR_DIR);
	rmdir(VENDOR_DIR);
	unlink(VENDOR_NODE);

	klog(6, "%d entries (%llu KiB) unpacked from " VENDOR_PARTNAME " after %ld ms\n", entries,
	     offset / 1024, elapsed_ms(&start));
	unlink(KMSG_NODE);
	execve("/init", argv, envp);
	fail("executing the recovery's /init");
}
