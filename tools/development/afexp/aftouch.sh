#!/system/bin/sh
# Touch AF on the rear camera: stream 640x480 YU12 for ~12 s with the torch on, AF on the centre at
# +1.5 s, on area $1 at +4.5 s, on area $2 at +8 s (areas as left,top,width,height in pixels)
T=/data/local/tmp; O=$T/aft; rm -rf $O; mkdir -p $O
node() { for d in /sys/class/video4linux/*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
V=$(node fimc.0.capture); SD=$(node S5C73M3); C="$T/v4l2-ctl -d $SD"; M="$T/media-ctl -d /dev/media0"
am force-stop org.lineageos.aperture; stop vendor.camera-provider-n7100; sleep 15
L=/sys/class/leds/white:flash/brightness; echo 8 > $L
for e in '"S5C73M3":0' '"S5C73M3-OIF":0' '"S5C73M3-OIF":2' '"s5p-mipi-csis.0":0' '"FIMC.0":0'; do $M -V "$e [fmt:VYUY8_2X8/640x480]" >/dev/null; done
area() { $T/afarea $SD $(echo "$1" | tr , ' '); $C -C auto_focus_area >> $O/log 2>&1; }
scan() { echo "$1 $(date +%s.%N)" >> $O/log; $C -c auto_focus_start=1
  for i in $(seq 14); do echo "$(date +%s.%N) $($C -C auto_focus_status 2>&1)" >> $O/log; sleep 0.15; done; }
echo "stream $(date +%s.%N)" > $O/log
$T/v4l2-ctl -d $V --set-fmt-video=width=640,height=480,pixelformat=YU12 --stream-mmap=4 --stream-count=360 --stream-to=$O/f.raw > $O/stream.log 2>&1 &
sleep 1.5; area 0,0,65535,65535; scan centre
sleep 0.5; area "$1"; scan spot1
sleep 1; area "$2"; scan spot2
area 0,0,65535,65535
wait; echo "end $(date +%s.%N)" >> $O/log
echo 0 > $L; start vendor.camera-provider-n7100
