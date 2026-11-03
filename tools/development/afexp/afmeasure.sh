#!/system/bin/sh
# Sharpness through AF scans: stream 640x480 YU12 for ~8 s; AF (normal) at +2 s, AF macro at +5 s
T=/data/local/tmp; O=$T/afm; rm -rf $O; mkdir -p $O
node() { for d in /sys/class/video4linux/*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
V=$(node fimc.0.capture); C="$T/v4l2-ctl -d $(node S5C73M3)"; M="$T/media-ctl -d /dev/media0"
am force-stop org.lineageos.aperture; stop vendor.camera-provider-n7100; sleep 20
# light the scene with the torch (the HAL is stopped, so the LED is free)
L=/sys/class/leds/white:flash/brightness; echo 8 > $L
for e in '"S5C73M3":0' '"S5C73M3-OIF":0' '"S5C73M3-OIF":2' '"s5p-mipi-csis.0":0' '"FIMC.0":0'; do $M -V "$e [fmt:VYUY8_2X8/640x480]"; done
echo "stream $(date +%s.%N)" > $O/log
$T/v4l2-ctl -d $V --set-fmt-video=width=640,height=480,pixelformat=YU12 --stream-mmap=4 --stream-count=240 --stream-to=$O/f.raw > $O/stream.log 2>&1 &
sleep 2; echo "normal $(date +%s.%N)" >> $O/log; $C -c auto_focus_start=1
for i in $(seq 25); do echo "$(date +%s.%N) $($C -C auto_focus_status 2>&1)" >> $O/log; sleep 0.1; done
echo "macro $(date +%s.%N)" >> $O/log; $C -c auto_focus_range=2; $C -c auto_focus_start=1
for i in $(seq 25); do echo "$(date +%s.%N) $($C -C auto_focus_status 2>&1)" >> $O/log; sleep 0.1; done
$C -c auto_focus_range=1
wait; echo "end $(date +%s.%N)" >> $O/log
logcat -b kernel -d | grep -i s5c73m3 | tail -5 >> $O/log
echo 0 > $L; start vendor.camera-provider-n7100
