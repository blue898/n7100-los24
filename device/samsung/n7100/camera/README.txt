Note II camera HAL: AOSP's external camera HAL (hardware/interfaces/camera/{device,provider}/default,
Android 17) adapted to the mainline exynos4-is driver.

Changes against AOSP:
- CameraProvider.cpp replaces the hotplug provider: fixed cameras from N7100Camera.cpp, registered
  as ICameraProvider/internal/0 with ids "0", ... (facing/orientation/lens data from the table).
- N7100Camera.cpp: the media pipeline runs in "sub-dev" mode, so the HAL sets the formats on every
  subdevice before S_FMT on the capture node (what media-ctl -V does): the stream size along the
  rear chain; for the front camera it enables its links and sets the fixed sensor/ISP formats
  before the capture node is opened (the first open powers the pipeline) and the FIMC FIFO format
  after it. Sizes come from the sensor subdevice (FIMC has no ENUM_FRAMESIZES/FRAMEINTERVALS/
  G_PARM) or a fixed list. Autofocus only for the rear camera; the front camera is fixed focus
  (AF mode OFF only).
- Torch (N7100Torch in N7100Camera.cpp): the rear camera's MAX77693 flash LED (white:flash) in
  torch mode, 140 mA in 8 steps (torch strength levels, default the full current). Torch mode
  for cameraserver while the camera is closed, FLASH_MODE_TORCH while it is open; status changes
  go to cameraserver through the provider, on a thread of their own. cameraserver looks for
  flash units only when a provider registers, so the provider registers once its cameras are
  found (at most 60 s). The torch is also the flash for stills (AE_MODE_ON_ALWAYS_FLASH, and
  ON_AUTO_FLASH below a mean luma of 50): lit from the precapture trigger, AE reports
  PRECAPTURE for 8 frames while the S5C73M3's ISP adjusts to the light, off after the still.
- Autofocus (rear camera): AF modes AUTO, CONTINUOUS_PICTURE (default for preview and stills),
  CONTINUOUS_VIDEO (default for video) and OFF. AUTO: an AF trigger runs one scan on the S5C73M3,
  on the request's AF region (tap-to-focus: the region's centre in stream pixels goes to
  V4L2_CID_S5C73M3_AF_AREA, kernel patch 0120) or the centre; ~0.5 s after stream on the HAL
  scans once by itself. CONTINUOUS_*: the sensor's continuous AF (V4L2_CID_FOCUS_AUTO) on the
  centre; an AF trigger locks the lens once the running scan is done (VIDEO: at once).
- AE compensation (rear camera): -4..4 in 1/2 EV steps, the S5C73M3's AE bias
  (V4L2_CID_AUTO_EXPOSURE_BIAS, an integer menu of -2..2 EV; its default was -0.5 EV before kernel
  patch 0122).
- V4L2 multi-planar API (FIMC capture is mplane only); input format YU12 (FIMC converts the
  sensor's YUV 4:2:2 to planar 4:2:0 in hardware, the HAL only copies), fixed 30 fps.
- Config /vendor/etc/camera/n7100_camera_config.xml (needs an <ignore/> element or the parser
  stops before reading the FPS limits).

Rear camera (S5C73M3 -> OIF -> CSIS0 -> FIMC0 = fimc.0.capture):
- boots its ISP firmware from its own F-ROM (sensor CML0801-M017, FW GKFH93).
- YUV sizes 320x240 .. 2304x1296 stream; 1632x1224 and 1920x1440 deliver no frames (listed in
  brokenSizes). 3264x2448 is left out by the FPS limit (software scaling of every frame).
- Autofocus works since kernel patch 0119 (2026-11-03 morning): the driver powered the lens
  supply (vdd-af) after the I/O supplies; Samsung's board code powers it before them. Moving
  only vdd-af ahead of vddio-host/vddio-cis is enough (variants A-E, s5c-variant.py in
  tools/development/afexp: 10 ms after all supplies doesn't help; Samsung's order with or
  without its delays, or just vdd-af first, does). Scans now end REACHED and the picture is
  sharp (sharpness x150 at ~25 cm, torch-lit), touch AF and continuous AF move the lens too,
  and the soft landing at power-off succeeds. Measurement: afexp-run.sh, aftouch.sh, afdemo.sh
  there (raw frames with the torch on, sharpness per region); in Aperture: apptest.sh with
  afpoll.sh (the AF controls while an app taps). The notes below are the search before that.
- Autofocus (history): the lens didn't move. Measured with the phone resting still (640x480 raw
  frames, ffmpeg blurdetect): sharpness stayed the same through single scans in normal and macro
  range; the ISP reported BUSY for ~0.3 s, then FAILED (continuous AF: FAILED all the time).
  CAM_AF_2.8V (GPM1-1, as in Samsung's T0 board code) is on while streaming; Samsung's VDD timing
  registers before the ROM boot made no difference.
  2026-11-03: not the firmware either. Samsung's driver always boots the ISP over SPI, LineageOS
  21 with SlimISP_GK.bin (GKFI03, newer than the F-ROM's GKFH93). Booted that way
  (boot_from_rom=0; kernel patch 0116 for the file name) the scans end the same: BUSY for
  ~0.6 s, then FAILED, sharpness unchanged (phone held at a monitor). Samsung's driver sends the
  same AF commands and no AF setup at init. Back to the ROM boot (camera start 1 s faster).
- The vid-dev mode of exynos4-is (driver configures the pipeline itself) is broken upstream:
  __video_try_or_set_format() passes "try" where fimc_pipeline_try_format() expects "set", so
  S_FMT asks the subdevices for TRY formats without a state and fails with EINVAL on open.

Front camera (S5K6A3 -> CSIS1 -> FIMC-LITE.1 -> FIMC-IS-ISP -> FIMC.1 = fimc.1.capture), camera
id 1: works with the stock FIMC-IS firmware since kernel patches 0107-0112 (Martin Jücker's
FIMC-IS fixes, see 2026-11-02 evening below).
- The ISP runs the sensor at 1296x732 and outputs 1280x720 (it keeps a 16x12 margin); FIMC scales
  to YU12. Sizes: 1280x720, 1024x576, 768x432, 512x288 (tools/development/frontsizes.sh). FIMC
  rounds heights up to multiples of 16; 640x360 and 256x144 hang the stream. 16:9 only.
- Aperture (2026-11-02): preview, photos (720x1280 JPEG, portrait) and video (1280x720, ~16 fps
  in room light, with sound); switching between the cameras works both ways. The picture is
  upright and mirrored like a selfie (orientation 270).
- CameraX probes a 320x240 stream after a session (not in the list: the HAL refuses it, CameraX
  carries on).
- No 4:3 sizes: cropping 960x720 out of the FIFO input (VIDIOC_S_SELECTION CROP on the capture
  node, tools/development/frontcrop.sh) gives striped frames with missing lines at 960x720 and
  640x480 and hangs at 320x240. Before kernel patch 0113 that crop request panicked the kernel
  (division by zero: the FIFO formats have no depth).
- Colours are warm under room light; the ISP's white balance is on (auto) - not compared under
  daylight yet. Lens data (focal length, aperture, sensor size) are estimates.
The notes below up to 2026-11-02 evening are the history of the search.
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
- 2026-11-02, five more fresh-boot tries, all failed the same way: the FIMC-IS firmware never
  answers HIC_OPEN_SENSOR (2 s timeout), the following HIC_POWER_DOWN gets ISR_NDONE 17
  (IS_ERROR_UNKNOWN). Ruled out:
  - power: while the open waits, CAM_SENSOR_A (2.8 V) and VT_CAM_1.8V are on, the reset line
    (GPM1-6) is released, CAM_B_CLKOUT (GPM2-2, function 3) toggles at 24 MHz from XUSBXTI
    (tools/development/fronttest3.sh);
  - MCLK drive strength: Samsung's T0 code sets GPM2-2 to the weakest drive (LV1), the mainline
    DT uses LV4; LV1 made no difference;
  - the rear camera's supplies (CAM_ISP_SEN_IO_1.8V, CAM_SENSOR_CORE_1.2V) on during the open
    (fronttest4.sh);
  - the host protocol, I thought: ISSR1-3 and the sensor_open_extended values match Samsung's
    Android driver. Wrong: ISSR4 has to be the DMA address of sensor_open_extended (the shared
    part of the parameter region), mainline sent the start of the region. See below.
- 2026-11-02 evening: Martin Jücker fixed the driver for the Galaxy Note 10.1 (same SoC and
  sensor): https://gitlab.com/exynos4-mainline/linux/-/merge_requests/28, branch
  v7.1.1-exynos4-camera of gitlab.com/martin.juecker/linux. Taken as kernel patches 0107-0112:
  - 0111 the sensor data address in ISSR4 (the reason HIC_OPEN_SENSOR never finished);
  - 0112 the ISP clock muxes after the power domain was off (they fell back to 24 MHz: the
    random ISP start timeouts);
  - 0107 timeouts in ms instead of jiffies, 0108 always power the ISP off (no more stuck ISP
    until reboot after a failed open), 0109 and 0110 error paths.
  With them and the stock firmware, tools/development/fronttest5.sh (his formats: SGRBG10_1X10
  1296x732 from the sensor to the ISP sink, YUV10_1X30 1296x732 on the ISP source,
  YUV10_1X30/1280x720 on the FIMC.1 FIFO pad) opens the sensor on the first try and streams
  YUYV 1280x720 at 14-17 fps; the exposure settles within 5 frames and the picture is real
  (a hand in front of a wall). Colours are warm (white balance under room light not checked),
  orientation not checked yet.

Graphics side (patches/external_minigbm-exynos-dumb.patch): the exynos minigbm backend
drops SCANOUT for YUV buffers (FIMD can't scan out NV12; the preview is composed by the GPU) and
doesn't pad 1-line BLOB buffers to 64 lines (a 5 MB JPEG buffer became 320 MB -> ENOMEM).

Media-ctl/v4l2-ctl for debugging: mainline\tools\bin\{media-ctl,v4l2-ctl}_static (AOSP
external/v4l-utils), push to /data/local/tmp.
