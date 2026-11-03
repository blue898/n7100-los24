#!/bin/bash
# Deep sleep cycles with Wi-Fi while USB stays plugged into this PC: the USB gadget's wakeup source
# (kernel patch 0118) is switched off for the test, so the phone can suspend as if unplugged.
# Counts suspends and what woke the phone, and pings it every 15 s over Wi-Fi.
# usage: sleeptest2.sh <phone ip> [seconds]
set -u
export MSYS_NO_PATHCONV=1
IP=$1; T=${2:-150}
adb -s ${ANDROID_SERIAL:-4d003387507e60b7} root >/dev/null 2>&1; sleep 6
for i in $(seq 30); do W=$(adb devices | awk '/_adb-tls-connect/ && $2 == "device" {print $1; exit}'); [ -n "$W" ] && break; sleep 2; done
a() { adb -s "$W" shell "$@"; }
[ "$(a id -u)" = 0 ] || { echo "adbd isn't root"; exit 1; }
P=/sys/devices/virtual/android_usb/android0/power/wakeup
a "echo disabled > $P; svc power stayon false; settings put global stay_on_while_plugged_in 0; input keyevent KEYCODE_SLEEP
  dmesg -C; echo 0 > /sys/class/rtc/rtc0/wakealarm; echo +$((T + 120)) > /sys/class/rtc/rtc0/wakealarm
  D=\$(( \$(date +%s) + T + 120 ))
  nohup sh -c \"while [ \\$(date +%s) -lt \$D ]; do sleep 2; done; echo enabled > $P; svc power stayon true; settings put global stay_on_while_plugged_in 7\" >/dev/null 2>&1 &
  echo start suspends=\$(cat /sys/power/suspend_stats/success)"
for i in $(seq $((T / 15))); do sleep 15; ping -n 1 -w 4000 $IP | grep -E "Reply|timed out|unreachable" | sed "s/^/$((i * 15))s: /"; done
a "echo end suspends=\$(cat /sys/power/suspend_stats/success) fail=\$(cat /sys/power/suspend_stats/fail)
  dmesg | grep -E 'PM: suspend (entry|exit)|silent reset|state changed|Active State|CP not ready|brcmf|failed to suspend' | awk '{print \$1, \$2, \$3, \$4, \$5, \$6, \$7}' | uniq -c -f2 | tail -30
  logcat -d -b main,system | grep -E 'CTRL-EVENT-(DISCONNECTED|CONNECTED)' | tail -3
  echo enabled > $P; svc power stayon true; settings put global stay_on_while_plugged_in 7"
