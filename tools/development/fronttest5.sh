#!/system/bin/sh
# Front camera with the FIMC-IS fixes from gitlab.com/exynos4-mainline/linux MR 28 (sensor data
# address in HIC_OPEN_SENSOR, ISP clock muxes after resume, ms timeouts). Pipeline and formats as
# in that work: S5K6A3 -> CSIS.1 -> FIMC-LITE.1 -> FIMC-IS-ISP -> FIMC.1 (FIFO) -> fimc.1.capture.
# Needs the development kernel with debugfs fimc_is/memory (the firmware's whole memory: image,
# log, shared region). While the capture node's open waits, samples the ISP I2C1 pins and
# controller (only while the ISP power domain is on); saves the firmware memory to
# /data/local/tmp/fimcis-mem.bin; if the open works, streams 20 YUYV 1280x720 frames to
# /data/local/tmp/front.yuv. Reboot afterwards when the open fails.
T=/data/local/tmp; cd $T; M="./media-ctl -d /dev/media0"; P=/vendor/bin/n7100tool
node() { for d in /sys/class/video4linux/video*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
V=$(node fimc.1.capture)
setenforce 0
mountpoint -q /sys/kernel/debug || mount -t debugfs debugfs /sys/kernel/debug
D=/sys/kernel/debug/fimc_is
[ -e $D/memory ] || { echo "no $D/memory: not the debug kernel"; exit 1; }
stop vendor.camera-provider-n7100
sleep 1
rm -f $T/open.ok $T/front.yuv
$M -r
$M -l "\"s5p-mipi-csis.1\":1 -> \"FIMC-LITE.1\":0 [1]"
$M -l "\"FIMC-LITE.1\":2 -> \"FIMC-IS-ISP\":0 [1]"
$M -l "\"FIMC-IS-ISP\":1 -> \"FIMC.1\":1 [1]"
F="SGRBG10_1X10/1296x732 field:none"
for e in "\"S5K6A3 16-0010\":0" "\"s5p-mipi-csis.1\":0" "\"s5p-mipi-csis.1\":1" "\"FIMC-LITE.1\":0" \
         "\"FIMC-LITE.1\":2" "\"FIMC-IS-ISP\":0"; do $M -V "$e [fmt:$F]" 2>&1; done
$M -V "\"FIMC-IS-ISP\":1 [fmt:YUV10_1X30/1296x732 field:none]" 2>&1
regs() {
  # GPM4-2/3 = ISP I2C1 SCL/SDA (function 2 in GPM4CON bits 8-15)
  r="gpm4 $($P peek 0x110002e0) $($P peek 0x110002e4) pd_isp $($P peek 0x10023ca4)"
  # ISP I2C1 controller (0x12140000: IICCON IICSTAT IICADD IICDS IICLC), only with the
  # ISP power domain on (status 7), a read with it off can abort
  [ "$($P peek 0x10023ca4)" = 0x00000007 ] &&
    r="$r i2c1 $($P peek 0x12140000) $($P peek 0x12140004) $($P peek 0x12140008) $($P peek 0x1214000c) $($P peek 0x12140010)"
  echo "$r"
}
echo "idle: $(regs)"
dmesg -c > /dev/null
# the first open powers the pipeline and opens the sensor; this process keeps it open
( exec 3<> $V && echo "open ok" && touch $T/open.ok && sleep 40 ) &
H=$!
for i in 1 2 3 4 5 6 7 8 9 10; do sleep 0.3; echo "t=$i: $(regs)"; [ -e $T/open.ok ] && break; done
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e $T/open.ok ] && break; kill -0 $H 2>/dev/null || break; sleep 0.5; done
dmesg | grep -iE "ISR_|fimc-is|FIMC-IS|timed out|initialization|setfile" | grep -v "Modules linked\|from " | tail -10
if [ -e $T/open.ok ]; then
  $M -V "\"FIMC.1\":1 [fmt:YUV10_1X30/1280x720 field:none]" 2>&1
  ./v4l2-ctl -d $V --set-fmt-video=width=1280,height=720,pixelformat=YUYV --stream-mmap=4 \
    --stream-count=20 --stream-to=$T/front.yuv > $T/front.log 2>&1 &
  S=$!
  for i in 1 2 3 4 5 6; do sleep 2; kill -0 $S 2>/dev/null || break; done
  kill $S 2>/dev/null
  echo "stream: $(stat -c %s $T/front.yuv 2>/dev/null || echo 0) bytes (one frame: 1843200)"
  tail -3 $T/front.log
  dmesg | grep -iE "ISR_|fimc|flite|csis|timed out" | tail -8
fi
cat $D/memory > $T/fimcis-mem.bin && echo "firmware memory: $(stat -c %s $T/fimcis-mem.bin) bytes in $T/fimcis-mem.bin"
kill $H 2>/dev/null
wait
