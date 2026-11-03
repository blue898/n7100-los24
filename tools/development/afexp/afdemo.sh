#!/system/bin/sh
# Focus demo: stream the rear camera at $1x$2 (planar YUV 4:2:2) with the torch on, then one AF scan
# per area $3...: "full" is the normal AF on the centre, else left,top,width,height in pixels
T=/data/local/tmp; O=$T/afd; rm -rf $O; mkdir -p $O
W=$1; H=$2; shift 2
node() { for d in /sys/class/video4linux/*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
V=$(node fimc.0.capture); SD=$(node S5C73M3); C="$T/v4l2-ctl -d $SD"; M="$T/media-ctl -d /dev/media0"
am force-stop org.lineageos.aperture; stop vendor.camera-provider-n7100; sleep 15
L=/sys/class/leds/white:flash/brightness; echo 8 > $L
for e in '"S5C73M3":0' '"S5C73M3-OIF":0' '"S5C73M3-OIF":2' '"s5p-mipi-csis.0":0' '"FIMC.0":0'; do $M -V "$e [fmt:VYUY8_2X8/${W}x$H]" >/dev/null; done
echo "stream $(date +%s.%N)" > $O/log
$T/v4l2-ctl -d $V --set-fmt-video=width=$W,height=$H,pixelformat=422P --stream-mmap=4 --stream-count=$((30 * (2 + 3 * $#))) --stream-to=$O/f.raw > $O/stream.log 2>&1 &
sleep 1.5
for a in "$@"; do
  [ "$a" = full ] && $T/afarea $SD 0 0 65535 65535 || $T/afarea $SD $(echo "$a" | tr , ' ')
  echo "area $(date +%s.%N) $a" >> $O/log; $C -c auto_focus_start=1
  for i in $(seq 13); do echo "$(date +%s.%N) $($C -C auto_focus_status 2>&1)" >> $O/log; sleep 0.15; done
done
$T/afarea $SD 0 0 65535 65535
wait; echo "end $(date +%s.%N)" >> $O/log
echo 0 > $L; start vendor.camera-provider-n7100
