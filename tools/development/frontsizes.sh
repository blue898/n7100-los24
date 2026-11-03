#!/system/bin/sh
# Front camera output sizes: FIMC.1 scales the ISP output (FIFO 1280x720) to each size as YU12,
# as the camera HAL reads it. Pipeline as in fronttest5.sh; run as root, camera HAL stopped.
T=/data/local/tmp; cd $T; M="./media-ctl -d /dev/media0"
node() { for d in /sys/class/video4linux/video*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
V=$(node fimc.1.capture)
stop vendor.camera-provider-n7100
sleep 1
$M -l "\"s5p-mipi-csis.1\":1 -> \"FIMC-LITE.1\":0 [1]"
$M -l "\"FIMC-LITE.1\":2 -> \"FIMC-IS-ISP\":0 [1]"
$M -l "\"FIMC-IS-ISP\":1 -> \"FIMC.1\":1 [1]"
F="SGRBG10_1X10/1296x732 field:none"
for e in "\"S5K6A3 16-0010\":0" "\"s5p-mipi-csis.1\":0" "\"s5p-mipi-csis.1\":1" "\"FIMC-LITE.1\":0" \
         "\"FIMC-LITE.1\":2" "\"FIMC-IS-ISP\":0"; do $M -V "$e [fmt:$F]"; done
$M -V "\"FIMC-IS-ISP\":1 [fmt:YUV10_1X30/1296x732 field:none]"
rm -f $T/open.ok
( exec 3<> $V && touch $T/open.ok && sleep 60 ) &
H=$!
for i in 1 2 3 4 5 6 7 8 9 10; do sleep 0.5; [ -e $T/open.ok ] && break; done
[ -e $T/open.ok ] || { echo "open failed"; kill $H; exit 1; }
$M -V "\"FIMC.1\":1 [fmt:YUV10_1X30/1280x720 field:none]"
for s in ${SIZES:-1280x720 960x540 640x360 320x180 640x480}; do
  w=${s%x*}; h=${s#*x}
  rm -f $T/size.yuv
  ./v4l2-ctl -d $V --set-fmt-video=width=$w,height=$h,pixelformat=YU12 --stream-mmap=4 \
    --stream-count=8 --stream-to=$T/size.yuv > $T/size.log 2>&1
  echo "$s: rc $? $(./v4l2-ctl -d $V --get-fmt-video | grep -E "Width/Height|Bytes per Line|Size Image" | tr -s " \t" " " | tr "\n" " ") file $(stat -c %s $T/size.yuv 2>/dev/null) (frame $((w * h * 3 / 2)))"
  cp $T/size.yuv $T/size-$s.yuv 2>/dev/null
done
kill $H 2>/dev/null
wait
start vendor.camera-provider-n7100
