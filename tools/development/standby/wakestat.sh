#!/bin/bash
# What wakes the phone from deep sleep: screen off and the USB gadget's wakeup source off (USB stays
# plugged into this PC), pm_debug_messages on for <seconds>; counts suspends, wake IRQs, modem resets.
# Runs on the phone in the background (USB adb drops while it sleeps), the result is fetched after.
# usage: wakestat.sh [seconds]
export MSYS_NO_PATHCONV=1 ANDROID_SERIAL=${ANDROID_SERIAL:-4d003387507e60b7}
T=${1:-90}
D=$(cd "$(dirname "$0")" && pwd -W 2>/dev/null || pwd)
[ "$(adb shell id -u)" = 0 ] || { adb root >/dev/null 2>&1; sleep 5; adb wait-for-device; }
cat > "$D/.wakestat-phone.sh" <<PHONE
P=/sys/devices/virtual/android_usb/android0/power/wakeup
O=/data/local/tmp/wakestat.txt
dmesg -C; echo 1 > /sys/power/pm_debug_messages; s1=\$(cat /sys/power/suspend_stats/success); f1=\$(cat /sys/power/suspend_stats/fail)
svc power stayon false; settings put global stay_on_while_plugged_in 0; input keyevent KEYCODE_SLEEP
echo disabled > \$P; sleep \$(($T / 2))
{ echo "active at \$(($T / 2)) s: userspace=[\$(cat /sys/power/wake_lock)]"; for w in /sys/class/wakeup/wakeup*; do t=\$(cat \$w/active_time_ms 2>/dev/null); [ "\$t" != 0 ] && echo "  \$(cat \$w/name) \$t ms"; done; dumpsys power | grep -E "^  [A-Z_]+_WAKE_LOCK|mWakefulness=" | head -5; } > /data/local/tmp/wakestat-mid.txt 2>&1
sleep \$(($T / 2)); echo enabled > \$P
svc power stayon true; settings put global stay_on_while_plugged_in 7; echo 0 > /sys/power/pm_debug_messages
{
echo "suspends in $T s: \$((\$(cat /sys/power/suspend_stats/success) - s1)), failed: \$((\$(cat /sys/power/suspend_stats/fail) - f1))"
echo "wake-up IRQs:"; dmesg | grep -o -E "Triggering wakeup from IRQ [0-9]+" | sort | uniq -c | sort -rn
for i in \$(dmesg | grep -o -E "Triggering wakeup from IRQ [0-9]+" | awk '{print \$5}' | sort -u); do grep -E "^ *\$i:" /proc/interrupts; done
echo "modem resets: \$(dmesg | grep -c 'silent reset')"
echo "modem link: runtime \$(cat /sys/bus/usb/devices/2-2/power/control 2>/dev/null)/\$(cat /sys/bus/usb/devices/2-2/power/runtime_status 2>/dev/null), reconnects \$(dmesg | grep -c 'trying reconnect'), EHCI power-on failures \$(dmesg | grep -c 'Power on failed'), modem online \$(dmesg | grep -c 'BOOTING -> ONLINE'), runtime PM retries off: \$(dmesg | grep -c 'runtime PM stays off')"
dmesg | grep -i -E "wakeup|abort" | grep -v -E "wake (en|dis)abled for irq" | head -8
cat /data/local/tmp/wakestat-mid.txt
} > \$O
echo done >> \$O
PHONE
adb push "$D/.wakestat-phone.sh" /data/local/tmp/wakestat-phone.sh >/dev/null
adb shell 'rm -f /data/local/tmp/wakestat.txt; nohup sh /data/local/tmp/wakestat-phone.sh > /dev/null 2>&1 &'
sleep $((T + 20))
for i in $(seq 30); do adb wait-for-device; adb shell 'grep -q ^done /data/local/tmp/wakestat.txt 2>/dev/null' && break; sleep 5; done
adb shell cat /data/local/tmp/wakestat.txt
