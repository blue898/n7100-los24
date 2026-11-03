Rear camera (S5C73M3) autofocus measurements, 2026-11-03. The phone rests still and looks at
something with detail at ~25 cm; the scripts light it with the torch and stop the camera HAL while
they stream (rooted debugging or a development build). On the phone in /data/local/tmp:
v4l2-ctl and media-ctl (tools/bin of the build machine, AOSP external/v4l-utils static builds) and
afarea (afarea/afarea.c, sets the AF area control of kernel patch 0120; v4l2-ctl splits its
rectangle at the commas).

- s5c-variant.py: patches the driver's power-on sequence (variants A-E) into a kernel tree, to
  build s5c73m3-<variant>.ko with "make modules".
- afexp-run.sh <variant>: installs s5c73m3-<variant>.ko (orig = unchanged), reboots and runs
  afmeasure.sh: 640x480 stream, AF scans in normal and macro range, sharpness per frame
  (sharp.py). B, C, D and E focus, orig and A don't (kernel patch 0119 is variant E).
- aftouch.sh <area1> <area2>: AF on the centre, then on two areas (left,top,width,height);
  sharp-touch.py rates the sharpness of given regions over time.
- afdemo.sh <w> <h> <area|full>...: a stream with one AF scan per area; afdemo-render.py makes
  an MP4 with the area and a label per scan and a grid of stills (ffmpeg, Windows font path).
- apptest.sh <outdir> "x y"...: taps in Aperture (video mode, torch on), screenshots before and
  after, the AF controls polled meanwhile (afpoll.sh) and CameraX's tap-to-focus results.
