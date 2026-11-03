"""Renders the focus demo (afdemo.sh): an MP4 with the AF area and a label per scan, and a grid
of stills, each taken when its scan has settled.
usage: afdemo-render.py DIR W H "label 1" "label 2" ...   (labels in the order of the areas)"""
import os, subprocess, sys
d, W, H, labels = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
FS = W * H * 2
n = os.path.getsize(f"{d}/f.raw") // FS
ev = [l.split() for l in open(f"{d}/log", encoding="utf-8")]
t0 = float(next(e for e in ev if e[0] == "stream")[1]); t1 = float(next(e for e in ev if e[0] == "end")[1])
areas = [(float(e[1]) - t0, e[2]) for e in ev if e[0] == "area"]
status = {}  # area index -> last status
for e in ev:
    if e and e[0][0].isdigit() and len(e) == 3:
        i = max(k for k, (t, _) in enumerate(areas) if float(e[0]) - t0 >= t) if float(e[0]) - t0 >= areas[0][0] else None
        if i is not None:
            status[i] = e[2]
fps = n / (t1 - t0)
FONT = r"fontfile='C\:/Windows/Fonts/arial.ttf'"
def box(a):
    if a == "full":
        return W // 3, H // 3, W // 3, H // 3
    return tuple(int(v) for v in a.split(","))
def result(i):
    return {"2": "scharf gestellt", "4": "kein Kontrast gefunden"}.get(status.get(i), "")
filters = []
for i, (t, a) in enumerate(areas):
    end = areas[i + 1][0] if i + 1 < len(areas) else t1 - t0
    x, y, w, h = box(a)
    en = f"enable='between(t,{t:.2f},{end:.2f})'"
    filters.append(f"drawbox=x={x}:y={y}:w={w}:h={h}:color=yellow@0.9:t=4:{en}")
    filters.append(f"drawtext={FONT}:text='{i + 1}  {labels[i]}':x=20:y=20:fontsize=34:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=8:{en}")
    filters.append(f"drawtext={FONT}:text='{result(i)}':x=20:y=72:fontsize=28:fontcolor=yellow:box=1:boxcolor=black@0.6:boxborderw=6:enable='between(t,{t + 1.4:.2f},{end:.2f})'")
filters.append(f"drawtext={FONT}:text='Vor dem ersten Fokussieren':x=20:y=20:fontsize=34:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=8:enable='lt(t,{areas[0][0]:.2f})'")
open(f"{d}/vf.txt", "w", encoding="utf-8").write(",\n".join(filters) + ",format=yuv420p")
raw = ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "yuv422p", "-s", f"{W}x{H}", "-r", f"{fps:.3f}", "-i", f"{d}/f.raw"]
subprocess.run(raw + ["-/vf", f"{d}/vf.txt", "-c:v", "libx264", "-crf", "20", "-preset", "slow", "-movflags", "+faststart", f"{d}/fokus-demo.mp4"], check=True)
# stills: the rest position, then each scan 0.3 s before the next one starts
times = [areas[0][0] - 0.2] + [(areas[i + 1][0] if i + 1 < len(areas) else t1 - t0) - 0.3 for i in range(len(areas))]
subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", f"{d}/fokus-demo.mp4", "-vf",
                "select='" + "+".join(f"eq(n,{int(t * fps)})" for t in times) + "',scale=640:480,tile=3x2",
                "-frames:v", "1", "-fps_mode", "passthrough", f"{d}/fokus-standbilder.jpg"], check=True)
print(f"{n} frames, {fps:.1f} fps, areas {[(round(t, 2), a, status.get(i)) for i, (t, a) in enumerate(areas)]}")
