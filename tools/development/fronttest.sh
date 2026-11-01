#!/system/bin/sh
# front camera: S5K6A3 -> CSIS1 -> FIMC-LITE.$L -> FIMC-IS-ISP -> FIMC.1 (fifo) -> video8
# video8 is opened (fd 3) before the FIFO input format is set: the default format the first
# open applies is not valid for a FIFO input
L=${1:-1}; T=/data/local/tmp; cd $T; M="./media-ctl -d /dev/media0"
stop vendor.camera-provider-n7100
sleep 1
$M -r
$M -l "\"s5p-mipi-csis.1\":1 -> \"FIMC-LITE.$L\":0 [1]"
$M -l "\"FIMC-LITE.$L\":2 -> \"FIMC-IS-ISP\":0 [1]"
$M -l "\"FIMC-IS-ISP\":1 -> \"FIMC.1\":1 [1]"
F=SGRBG10_1X10/1296x732
for e in "\"S5K6A3 16-0010\":0" "\"s5p-mipi-csis.1\":0" "\"FIMC-LITE.$L\":0" "\"FIMC-IS-ISP\":0"; do $M -V "$e [fmt:$F]" 2>&1; done
dmesg -c > /dev/null
exec 3<> /dev/video8 || { echo "open failed"; dmesg | tail -5; exit 1; }
echo "video8 open"
$M -V "\"FIMC.1\":1 [fmt:YUV10_1X30/1280x720]" 2>&1

./v4l2-ctl -d /dev/video8 --set-fmt-video=width=1280,height=720,pixelformat=YU12 --stream-mmap=4 --stream-count=20 --stream-to=$T/front.raw > $T/front.log 2>&1 &
P=$!
for i in 1 2 3 4 5 6 7 8; do
  sleep 2
  r=""; for o in 0 4 8 12 16 20; do r="$r $(/vendor/bin/n7100tool peek $((0x11890000 + o)) 2>/dev/null)"; done
  echo "t=$((i*2)) csis1[00..14]:$r bytes=$(stat -c %s $T/front.raw 2>/dev/null)"
done
kill $P 2>/dev/null
exec 3>&-
tail -5 $T/front.log
dmesg | grep -vE "avc|audit|healthd|binder|servicemanager|logd|unwind|show_stack|dump_stack|__warn|warn_slow|Exception|\] [0-9a-f]{4}:" | tail -20
