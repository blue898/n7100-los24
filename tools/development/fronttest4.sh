#!/system/bin/sh
# Front camera with the rear camera powered: does the FIMC-IS sensor open succeed when the
# S5C73M3's supplies (CAM_ISP_SEN_IO_1.8V, CAM_SENSOR_CORE_1.2V, ...) are on? Reboot afterwards.
T=/data/local/tmp; cd $T; M="./media-ctl -d /dev/media0"
node() { for d in /sys/class/video4linux/video*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
rails() { for d in /sys/class/regulator/regulator.*; do case "$(cat $d/name)" in VT_CAM_1.8V|CAM_SENSOR_A|CAM_ISP_SEN_IO_1.8V|CAM_SENSOR_CORE_1.2V|CAM_AF_2.8V) echo -n " $(cat $d/name)=$(cat $d/state)";; esac; done; echo; }
R=$(node fimc.0.capture); V=$(node fimc.1.capture)
stop vendor.camera-provider-n7100
sleep 1
exec 4<> $R && echo "rear $R open"
sleep 3
echo "rails with rear open:$(rails)"
$M -l "\"s5p-mipi-csis.1\":1 -> \"FIMC-LITE.1\":0 [1]"
$M -l "\"FIMC-LITE.1\":2 -> \"FIMC-IS-ISP\":0 [1]"
$M -l "\"FIMC-IS-ISP\":1 -> \"FIMC.1\":1 [1]"
F=SGRBG10_1X10/1296x732
for e in "\"S5K6A3 16-0010\":0" "\"s5p-mipi-csis.1\":0" "\"FIMC-LITE.1\":0" "\"FIMC-IS-ISP\":0"; do $M -V "$e [fmt:$F]" 2>&1; done
dmesg -c > /dev/null
( exec 3<> $V && echo "front $V open OK" ) || echo "front open failed"
dmesg | grep -iE "ISR_|fimc-is|FIMC-IS|timed out|initialization|setfile|firmware" | grep -v "Modules linked\|from " | tail -8
exec 4>&-
