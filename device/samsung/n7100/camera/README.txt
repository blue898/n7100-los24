Note II camera HAL: AOSP's external camera HAL (hardware/interfaces/camera/{device,provider}/default,
Android 17) adapted to the mainline exynos4-is driver.

Changes against AOSP:
- CameraProvider.cpp replaces the hotplug provider: fixed cameras from N7100Camera.cpp, registered
  as ICameraProvider/internal/0 with ids "0", ... (facing/orientation/lens data from the table).
- N7100Camera.cpp: the media pipeline runs in "sub-dev" mode, so the HAL sets the stream size on
  every subdevice before S_FMT on the capture node (what media-ctl -V does), enumerates sizes on
  the sensor subdevice (FIMC has no ENUM_FRAMESIZES/FRAMEINTERVALS/G_PARM) and turns on continuous
  autofocus.
- V4L2 multi-planar API (FIMC capture is mplane only); input format YU12 (FIMC converts the
  sensor's YUV 4:2:2 to planar 4:2:0 in hardware, the HAL only copies), fixed 30 fps.
- Config /vendor/etc/camera/n7100_camera_config.xml (needs an <ignore/> element or the parser
  stops before reading the FPS limits).

Rear camera (S5C73M3 -> OIF -> CSIS0 -> FIMC0 = fimc.0.capture):
- boots its ISP firmware from its own F-ROM (sensor CML0801-M017, FW GKFH93).
- YUV sizes 320x240 .. 2304x1296 stream; 1632x1224 and 1920x1440 deliver no frames (listed in
  brokenSizes). 3264x2448 is left out by the FPS limit (software scaling of every frame).
- Autofocus: the lens doesn't move. Measured with the phone resting still (640x480 raw frames,
  ffmpeg blurdetect): sharpness stays the same through single scans in normal and macro range;
  the ISP reports BUSY for ~0.3 s, then FAILED (continuous AF: FAILED all the time). CAM_AF_2.8V
  (GPM1-1, as in Samsung's T0 board code) is on while streaming; Samsung's VDD timing registers
  before the ROM boot made no difference. Mechanical fault or something Samsung's stack does that
  the mainline driver doesn't - open. The HAL runs single scans on AF triggers and reports the
  real result (NOT_FOCUSED_LOCKED), continuous AF is off.
- The vid-dev mode of exynos4-is (driver configures the pipeline itself) is broken upstream:
  __video_try_or_set_format() passes "try" where fimc_pipeline_try_format() expects "set", so
  S_FMT asks the subdevices for TRY formats without a state and fails with EINVAL on open.

Front camera (S5K6A3 -> CSIS1 -> FIMC-LITE0 -> FIMC-IS-ISP -> FIMC1): FIMC-IS firmware and setfile
from the stock system load and initialise ("Setfile info: E4412 2012/05/25 VERSION 2.30"), but
HIC_STREAM_ON times out in the ISP firmware (ISR_NDONE 5, IS_ERROR_TIME_OUT): no frames reach the
ISP. Not in the HAL yet.
- Samsung's T0 board code (midas-camera.c) feeds the S5K6A3 through FIMC-LITE B (FIMC-LITE.1),
  1 lane, settle 18, sclk_cam1 = mainline DT (except FIMC-LITE.0 in my first test). With
  FIMC-LITE.1 STREAMON hung without an error; CSIS1 stayed disabled (CTRL 0x00100000).
- The ISP start itself is unreliable: sometimes "initialization sequence completed", sometimes
  fimc_is_hw_initialize() times out on open, followed by ISR_NDONE 17 (0x3e8). After a failed
  or killed attempt the ISP stays stuck until reboot (processes hang in fimc_is_wait_event).
  Suspicion: the stock Android FIMC-IS firmware (E4412 2012/04/16) isn't the Tizen firmware
  the mainline driver was written against.
- 2026-11-01, kernel 7.3.1, SELinux enforcing (tools/development/fronttest.sh: media-ctl -r, links
  CSIS1 -> FIMC-LITE.1 -> FIMC-IS-ISP -> FIMC.1 FIFO, SGRBG10 1296x732 up to the ISP sink):
  - video8 must be opened before the FIMC.1 FIFO pad is set to YUV10_1X30/1280x720: the
    default format the first open applies (YUYV 640x480) is invalid for a FIFO input (EINVAL);
    without the FIFO format STREAMON fails link validation (EPIPE).
  - On a fresh boot the open powers the pipeline and the ISP comes up with the stock firmware
    in 2 of 4 tries ("initialization sequence completed"); otherwise the open fails with ETIME
    (fimc_is_wait_event in fimc_is_hw_initialize) or EINVAL before the ISP is powered.
  - When it does come up, STREAMON succeeds and CSIS1 is enabled (CTRL 0x101), but no frame
    arrives within 10 s and no ISP error is reported.
  - After any failed attempt the ISP stays stuck until reboot (HIC_POWER_DOWN: Not Ready).
  Open: whether the S5K6A3 streams at all (CSIS1 interrupt status), the sensor power-up timing
  for the ISP firmware's I2C, and whether the Android firmware (E4412 2012/04/16) speaks the
  protocol of the mainline driver.
- Test script: tools/development/fronttest.sh (stops the camera HAL; links + formats via media-ctl,
  stream on video8, CSIS1 registers at 0x11890000 while it waits). Reboot between runs.

Graphics side (patches/external_minigbm-exynos-dumb.patch): the exynos minigbm backend
drops SCANOUT for YUV buffers (FIMD can't scan out NV12; the preview is composed by the GPU) and
doesn't pad 1-line BLOB buffers to 64 lines (a 5 MB JPEG buffer became 320 MB -> ENOMEM).

Media-ctl/v4l2-ctl for debugging: mainline\tools\bin\{media-ctl,v4l2-ctl}_static (AOSP
external/v4l-utils), push to /data/local/tmp.
