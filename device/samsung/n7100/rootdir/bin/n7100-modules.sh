#!/vendor/bin/sh
# Modules nothing on Android loads by itself, run once by init at early-init:
# - the netfilter modules behind netd's iptables rules (tables, xt_* matches and targets incl.
#   xt_quota2): without them iptables-restore fails, netd exits and takes zygote/system_server
#   down with it
# - modules the kernel would request_module() (there is no modprobe helper on Android): zram for
#   the swap in fstab, FAT/exFAT for SD cards, brcmfmac's firmware-vendor module (brcmfmac-wcc,
#   else wlan0 never shows up), bridge for tethering and WireGuard for VPN apps
# - the LED timer trigger: the lights HAL blinks the status LED through it (in hardware, so the
#   LED keeps blinking in deep sleep)
M=/vendor/lib/modules
for m in $(cat /vendor/etc/n7100-modules.netfilter) zram vfat exfat brcmfmac-wcc bridge wireguard \
         ledtrig-timer; do
    /vendor/bin/modprobe -d $M $m 2>/dev/null || echo "n7100-modules: loading $m failed" > /dev/kmsg
done
echo "n7100-modules: modules loaded" > /dev/kmsg
