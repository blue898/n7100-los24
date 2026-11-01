"""Read the previous boot's ramoops (pstore) area from TWRP via /dev/mem and decode it.

Layout from 0002-n710x-ramoops.patch: 2 MiB at 0x46000000, record-size 0x40000 (4 dmesg dumps),
console-size 0x80000, pmsg-size 0x80000. TWRP's 3.4 kernel doesn't know ramoops, but leaves the
area alone, so after a warm reset into TWRP the zones still hold the last mainline boot.
usage: py -3.14 -I tools/twrp_ramoops.py [outdir]
"""
import os
import struct
import subprocess
import sys
from pathlib import Path

BASE, SIZE = 0x46000000, 0x200000
ZONES = [(f"dmesg-{i}", i * 0x40000, 0x40000) for i in range(4)] + [
    ("console", 0x100000, 0x80000), ("pmsg", 0x180000, 0x80000)]
SIG = 0x43474244  # "DBGC"


def adb(*args, **kw):
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    env.setdefault("ANDROID_SERIAL", "4d003387507e60b7")  # only the Note II
    return subprocess.run(["adb", *args], check=True, env=env, **kw)


def zone_text(buf):
    sig, start, size = struct.unpack_from("<III", buf, 0)
    if sig != SIG:
        return None
    data = buf[12:]
    if size >= len(data) or start >= len(data):
        return None
    # ring buffer: once full, the oldest data starts at 'start'
    raw = data[start:size] + data[:start] if size == len(data) else data[:size]
    return raw.decode("utf-8", "replace")


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "ramoops")
    out.mkdir(parents=True, exist_ok=True)
    adb("shell", f"dd if=/dev/mem of=/tmp/ramoops.bin bs=4096 skip={BASE // 4096} "
                 f"count={SIZE // 4096} 2>/dev/null")
    adb("pull", "/tmp/ramoops.bin", str(out / "ramoops.bin"), capture_output=True)
    mem = (out / "ramoops.bin").read_bytes()
    for name, off, size in ZONES:
        text = zone_text(mem[off:off + size])
        if text:
            (out / f"{name}.txt").write_text(text, encoding="utf-8")
            print(f"{name}: {len(text)} chars -> {out / (name + '.txt')}")
        else:
            print(f"{name}: empty")


if __name__ == "__main__":
    main()
