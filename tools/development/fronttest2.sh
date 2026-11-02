#!/system/bin/sh
# Front camera: S5K6A3 -> CSIS1 -> FIMC-LITE.$L -> FIMC-IS-ISP -> FIMC.1 (FIFO) -> fimc.1.capture
# Like fronttest.sh, but finds the capture node by name (the numbers change between boots) and
# logs the CSIS1 interrupt source (0x14): bit 4..7 frame start, 0..3 frame end per virtual channel,
# errors above. Reboot afterwards: a failed ISP start leaves the ISP stuck.
L=${1:-1}; T=/data/local/tmp; cd $T; M="./media-ctl -d /dev/media0"
node() { for d in /sys/class/video4linux/video*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
V=$(node fimc.1.capture)
echo "capture node $V"
stop vendor.camera-provider-n7100
sleep 1
$M -r
$M -l "\"s5p-mipi-csis.1\":1 -> \"FIMC-LITE.$L\":0 [1]"
$M -l "\"FIMC-LITE.$L\":2 -> \"FIMC-IS-ISP\":0 [1]"
$M -l "\"FIMC-IS-ISP\":1 -> \"FIMC.1\":1 [1]"
F=SGRBG10_1X10/1296x732
for e in "\"S5K6A3 16-0010\":0" "\"s5p-mipi-csis.1\":0" "\"FIMC-LITE.$L\":0" "\"FIMC-IS-ISP\":0"; do $M -V "$e [fmt:$F]" 2>&1; done
dmesg -c > /dev/null
exec 3<> $V || { echo "open failed"; dmesg | tail -8; exit 1; }
echo "$V open"
$M -V "\"FIMC.1\":1 [fmt:YUV10_1X30/1280x720]" 2>&1
P=$((0x11890000))
regs() { r=""; for o in 0 4 8 12 16 20; do r="$r $(/vendor/bin/n7100tool peek $((P + o)) 2>/dev/null)"; done; echo "$r"; }
echo "before: csis1[00..14]:$(regs)"
./v4l2-ctl -d $V --set-fmt-video=width=1280,height=720,pixelformat=YU12 --stream-mmap=4 --stream-count=20 --stream-to=$T/front.raw > $T/front.log 2>&1 &
PID=$!
CS=$(for d in /sys/class/video4linux/v4l-subdev*; do [ "$(cat $d/name)" = "s5p-mipi-csis.1" ] && echo /dev/${d##*/}; done)
for i in 1 2 3 4 5 6; do
  sleep 1
  echo "t=$i csis1[00..14]:$(regs) bytes=$(stat -c %s $T/front.raw 2>/dev/null)"
done
# the CSIS driver counts frame starts/ends and errors: VIDIOC_LOG_STATUS prints them to the log
./v4l2-ctl -d $CS --log-status > /dev/null 2>&1
kill $PID 2>/dev/null
exec 3>&-
tail -5 $T/front.log
dmesg | grep -iE "fimc|csis|s5k6a3|is_|isp|lite" | tail -25
