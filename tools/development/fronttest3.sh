#!/system/bin/sh
# Front camera power-up probe: while the capture node's open waits for the FIMC-IS firmware to
# open the S5K6A3, sample the sensor's supplies, reset line and clock registers.
# Reboot afterwards when the open fails (the ISP stays stuck).
T=/data/local/tmp; cd $T; M="./media-ctl -d /dev/media0"; P=/vendor/bin/n7100tool
node() { for d in /sys/class/video4linux/video*; do [ "$(cat $d/name)" = "$1" ] && echo /dev/${d##*/}; done; }
V=$(node fimc.1.capture)
stop vendor.camera-provider-n7100
sleep 1
$M -r
$M -l "\"s5p-mipi-csis.1\":1 -> \"FIMC-LITE.1\":0 [1]"
$M -l "\"FIMC-LITE.1\":2 -> \"FIMC-IS-ISP\":0 [1]"
$M -l "\"FIMC-IS-ISP\":1 -> \"FIMC.1\":1 [1]"
F=SGRBG10_1X10/1296x732
for e in "\"S5K6A3 16-0010\":0" "\"s5p-mipi-csis.1\":0" "\"FIMC-LITE.1\":0" "\"FIMC-IS-ISP\":0"; do $M -V "$e [fmt:$F]" 2>&1; done
regs() {
  r=""
  for d in /sys/class/regulator/regulator.*; do
    case "$(cat $d/name 2>/dev/null)" in
      VT_CAM_1.8V|CAM_SENSOR_A|CAM_ISP_SEN_IO_1.8V|CAM_SENSOR_CORE_1.2V|CAM_AF_2.8V|VMIPI_1.0V|VMIPI_1.8V|VDDQ_M1_M2_1.8V)
        r="$r $(cat $d/name)=$(cat $d/state)";;
    esac
  done
  # GPM0/GPM1/GPM2: CON, DAT; camera clock source, divider, mask, gates (CMU 0x1003c...)
  echo "$r gpm0 $($P peek 0x11000260) $($P peek 0x11000264) gpm1 $($P peek 0x11000280) $($P peek 0x11000284) gpm2 $($P peek 0x110002a0) $($P peek 0x110002a4) src_cam0 $($P peek 0x1003c220) div_cam0 $($P peek 0x1003c520) mask_cam $($P peek 0x1003c320) gate_cam $($P peek 0x1003c920)"
}
echo "idle: $(regs)"
dmesg -c > /dev/null
( exec 3<> $V && echo "open ok" && sleep 3 ) &
for i in 1 2 3 4 5 6; do sleep 0.4; echo "t=$i: $(regs)"; done
wait
dmesg | grep -iE "ISR_|fimc-is|FIMC-IS|timed out|initialization|setfile" | grep -v "Modules linked\|from " | tail -8
