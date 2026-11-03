"""Sharpness per region through the touch AF test (aftouch.sh): 640x480 YU12 frames.
usage: sharp-touch.py f.raw log name=l,t,w,h ..."""
import sys
raw, log = sys.argv[1], sys.argv[2]
regions = [(a.split("=")[0], [int(v) for v in a.split("=")[1].split(",")]) for a in sys.argv[3:]]
W, H, FS = 640, 480, 640 * 480 * 2  # the buffers are 2 bytes per pixel, Y first
d = open(raw, "rb").read(); n = len(d) // FS
ev = [l.split() for l in open(log) if l.split() and l.split()[0] in ("stream", "centre", "spot1", "spot2", "end")]
t0 = float(ev[0][1]); t1 = float(ev[-1][1])
marks = {name: float(t) - t0 for name, t in ev[1:-1]}
dt = (t1 - t0) / n
def sharp(y, l, t, w, h):
    s = c = 0
    for yy in range(t + 1, t + h - 1, 2):
        row = yy * W
        for xx in range(l + 1, l + w - 1, 2):
            p = y[row + xx]
            g = 4 * p - y[row + xx - 1] - y[row + xx + 1] - y[row + xx - W] - y[row + xx + W]
            s += g * g; c += 1
    return s / c
print("frame  t      " + "  ".join(f"{name:>8}" for name, _ in regions) + "  mean")
for i in range(0, n, 3):
    y = d[i * FS:i * FS + W * H]
    t = i * dt
    tag = " ".join(k for k, v in marks.items() if t <= v < t + 3 * dt)
    print(f"{i:4d} {t:5.2f}s " + "  ".join(f"{sharp(y, *r):8.1f}" for _, r in regions)
          + f"  {sum(y[::97]) / len(y[::97]):5.1f} {tag}")
