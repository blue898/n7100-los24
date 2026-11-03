Deep sleep measurements, 2026-11-03 (rooted debugging; the phone stays plugged into the PC).
- wakestat.sh [seconds]: screen off, the USB gadget's wakeup source off (kernel patch 0118 would keep
  the phone awake while the PC uses USB), pm_debug_messages on; counts suspends, the IRQs that woke
  the phone (/sys/power/pm_wakeup_irq needs CONFIG_PM_SLEEP_DEBUG), modem resets and reconnects, the
  CP link's runtime PM state, and lists the active wakeup sources halfway. Runs on the phone in the
  background: USB adb drops while the phone sleeps.
- sleeptest2.sh <phone ip> [seconds]: the same over Wi-Fi adb, pinging the phone every 15 s; each
  ping must be answered while it sleeps (Wi-Fi WoWLAN with packet filters, kernel patches 0121, 0124).
