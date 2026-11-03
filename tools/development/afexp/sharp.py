"""Sharpness of the frames of afmeasure.sh (640x480 YU12 in 614400-byte buffers).

usage: sharp.py <f.raw> [log]
Prints the luma mean and a Laplacian energy of the image centre every 4th frame, then a
summary per phase: before the AF scans, the normal-range scan (+2 s), the macro scan (+~6.6 s,
from the log if given). A lens that moves changes the sharpness a lot within a scan; noise and
a still lens keep it within a few percent.
"""
import sys

W, H, FRAME, FPS = 640, 480, 614400, 29.8
data = open(sys.argv[1], "rb").read()
n = len(data) // FRAME

t_normal, t_macro = 2.0, 6.6
if len(sys.argv) > 2:
    t0 = None
    for line in open(sys.argv[2]):
        f = line.split()
        if len(f) >= 2 and f[0] == "stream":
            t0 = float(f[1])
        elif len(f) >= 2 and f[0] == "normal" and t0:
            t_normal = float(f[1]) - t0
        elif len(f) >= 2 and f[0] == "macro" and t0:
            t_macro = float(f[1]) - t0


def sharpness(i):
    y = data[i * FRAME:i * FRAME + W * H]
    s = m = c = 0
    for r in range(140, 340, 2):
        row = r * W
        for col in range(200, 440, 2):
            p = row + col
            v = 4 * y[p] - y[p - 1] - y[p + 1] - y[p - W] - y[p + W]
            s += v * v
            m += y[p]
            c += 1
    return m / c, s / c


phases = {"before": [], "normal": [], "macro": []}
for i in range(0, n, 2):
    t = i / FPS
    mean, sh = sharpness(i)
    phase = "before" if t < t_normal else "normal" if t < t_macro else "macro"
    phases[phase].append(sh)
    if i % 8 == 0:
        print(f"{i:3d} t={t:5.2f}s mean={mean:5.1f} sharp={sh:8.1f} {phase}")

print(f"frames {n}, normal scan at {t_normal:.2f} s, macro scan at {t_macro:.2f} s")
for phase, v in phases.items():
    if v:
        lo, hi, avg = min(v), max(v), sum(v) / len(v)
        print(f"{phase:7s} min {lo:8.1f} max {hi:8.1f} mean {avg:8.1f} spread {(hi - lo) / avg:5.2f}")
base = phases["before"]
scan = phases["normal"] + phases["macro"]
if base and scan:
    b = (max(base) - min(base)) / (sum(base) / len(base))
    s = (max(scan) - min(scan)) / (sum(scan) / len(scan))
    print("verdict:", "LENS MOVES" if s > max(0.3, 3 * b) else "lens still",
          f"(spread before {b:.2f}, during the scans {s:.2f})")
if base and sum(base) / len(base) < 5:
    print("warning: almost no detail before the scans - dark or flat scene?")
