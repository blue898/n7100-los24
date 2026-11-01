//! Small helper for the GT-N7100 mainline initramfs.
//!
//!   n7100tool peek <addr>             read a 32-bit MMIO register via /dev/mem
//!   n7100tool poke <addr> <value>     write a 32-bit MMIO register
//!   n7100tool usbfix [interval_ms]    keep dwc2 out of L1/L2 power states, reconnect if EP0 dies (daemon)
//!   n7100tool i2cget <bus> <addr> <reg>   read an I2C register, even of a device a driver owns
//!   n7100tool reboot <mode>           reboot with a reboot-mode argument (recovery, download, ...)
//!   n7100tool watchdog <timeout_s> <interval_s>   keep /dev/watchdog fed (daemon)
//!
//! usbfix works around dwc2 bugs on Exynos: after a host suspend/resume (L2, no_clock_gating) or a
//! failed LPM L1 exit the driver keeps lx_state != L0 and rejects every request with -EAGAIN, so the
//! gadget goes dead. We mask the suspend/LPM interrupts, clear LPM capability so the host can't
//! enter L1, and as a last resort re-bind the UDC when EP0 stays disarmed while configured.

use std::ffi::CString;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::os::fd::AsRawFd;
use std::os::unix::fs::OpenOptionsExt;
use std::{env, process, thread, time::Duration};

const DWC2_BASE: usize = 0x1248_0000;
const GINTMSK: usize = 0x018;
const GLPMCFG: usize = 0x054;
const DOEPCTL0: usize = 0xB00;
const GINTSTS_OTGINT: u32 = 1 << 2; // session end -> dwc2_hsotg_disconnect() -> L3
const GINTSTS_USBSUSP: u32 = 1 << 11; // bus suspend -> L2, never left on Exynos
const GINTSTS_LPMTRANRCVD: u32 = 1 << 27; // LPM -> L1
const GINTSTS_CONIDSTSCHNG: u32 = 1 << 28; // ID pin change -> role switch
const GINTSTS_SESSREQINT: u32 = 1 << 30; // session request -> dwc2_hsotg_disconnect() -> L3
const IRQS_OFF: u32 = GINTSTS_OTGINT | GINTSTS_USBSUSP | GINTSTS_LPMTRANRCVD | GINTSTS_CONIDSTSCHNG | GINTSTS_SESSREQINT;
const GLPMCFG_LPMCAP: u32 = 1 << 0;
const DXEPCTL_EPENA: u32 = 1 << 31;
// configfs gadget: Android mounts configfs at /config, the initramfs/Alpine at /sys/kernel/config
const GADGET_UDC: [&str; 2] = ["/config/usb_gadget/g1/UDC", "/sys/kernel/config/usb_gadget/g1/UDC"];

extern "C" {
    fn mmap(addr: *mut u8, len: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut u8;
    fn sync();
    fn syscall(n: i32, ...) -> i32;
    fn ioctl(fd: i32, req: i32, ...) -> i32;
}

struct Mmio {
    _file: File,
    base: *mut u8,
}

impl Mmio {
    fn map(page: usize) -> Mmio {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .custom_flags(0o4010000) // O_SYNC
            .open("/dev/mem")
            .unwrap_or_else(|e| die(&format!("open /dev/mem: {e}")));
        let base = unsafe { mmap(std::ptr::null_mut(), 4096, 3, 1, file.as_raw_fd(), page as i64) };
        if base as isize == -1 {
            die(&format!("mmap {page:#x}: {}", std::io::Error::last_os_error()));
        }
        Mmio { _file: file, base }
    }
    fn read(&self, off: usize) -> u32 {
        unsafe { std::ptr::read_volatile(self.base.add(off) as *const u32) }
    }
    fn write(&self, off: usize, v: u32) {
        unsafe { std::ptr::write_volatile(self.base.add(off) as *mut u32, v) }
    }
}

fn die(msg: &str) -> ! {
    eprintln!("n7100tool: {msg}");
    process::exit(1)
}

fn kmsg(msg: &str) {
    // one write() per record, otherwise /dev/kmsg splits the line
    if let Ok(mut f) = OpenOptions::new().write(true).open("/dev/kmsg") {
        let _ = f.write_all(format!("n7100tool: {msg}\n").as_bytes());
    }
}

fn udc_name() -> Option<String> {
    std::fs::read_dir("/sys/class/udc").ok()?.flatten().next().map(|e| e.file_name().to_string_lossy().into_owned())
}

fn udc_state(udc: &str) -> String {
    std::fs::read_to_string(format!("/sys/class/udc/{udc}/state")).unwrap_or_default().trim().to_string()
}

/// Is the configfs gadget currently bound to `udc`? An empty UDC file means the system (e.g.
/// Android's USB state machine while adbd restarts) unbound it on purpose.
fn gadget_bound(udc: &str) -> bool {
    GADGET_UDC
        .iter()
        .find(|p| std::path::Path::new(p).exists())
        .is_some_and(|p| std::fs::read_to_string(p).unwrap_or_default().trim() == udc)
}

/// Re-bind the gadget to the UDC, but only if it is currently bound.
fn rebind(udc: &str) -> bool {
    let Some(path) = GADGET_UDC.iter().find(|p| std::path::Path::new(p).exists()) else { return false };
    if !gadget_bound(udc) {
        return false;
    }
    let _ = std::fs::write(path, "\n");
    thread::sleep(Duration::from_millis(500));
    let _ = std::fs::write(path, udc);
    true
}

fn parse(s: &str) -> usize {
    let r = match s.strip_prefix("0x") {
        Some(h) => usize::from_str_radix(h, 16),
        None => s.parse(),
    };
    r.unwrap_or_else(|_| die(&format!("bad number {s}")))
}

fn split(addr: usize) -> (usize, usize) {
    (addr & !0xfff, addr & 0xfff)
}

fn usbfix(interval_ms: u64) -> ! {
    let m = Mmio::map(DWC2_BASE);
    let udc = udc_name().unwrap_or_else(|| die("no UDC"));
    kmsg(&format!("usbfix running on {udc}, interval {interval_ms} ms"));
    let check_every = (250 / interval_ms.max(1)).max(1);
    let (mut tick, mut bad, mut cooldown) = (0u64, 0u32, 0u64);
    let mut was_bound = false;
    loop {
        let v = m.read(GINTMSK);
        let masked = v & !IRQS_OFF;
        if masked != v {
            m.write(GINTMSK, masked);
            kmsg(&format!("masked suspend/LPM/OTG irqs (GINTMSK {v:#010x} -> {masked:#010x})"));
        }
        let l = m.read(GLPMCFG);
        if l & GLPMCFG_LPMCAP != 0 {
            m.write(GLPMCFG, l & !GLPMCFG_LPMCAP);
            kmsg(&format!("disabled LPM (GLPMCFG {l:#010x} -> {:#010x})", l & !GLPMCFG_LPMCAP));
        }
        tick += 1;
        // Only judge a bound gadget, and give a fresh (re)bind time to enumerate: otherwise the
        // "not attached" time while Android had it unbound counts against it and we'd tear the
        // new binding down before the host ever sees it.
        if tick % check_every == 0 {
            let bound = gadget_bound(&udc);
            if bound && !was_bound {
                bad = 0;
                cooldown = tick + 40 * check_every; // ~10 s
            }
            if !bound {
                bad = 0;
            }
            was_bound = bound;
        }
        if tick % check_every == 0 && tick >= cooldown && was_bound {
            // Healthy: configured, and EP0 OUT armed (it waits for the next SETUP packet).
            // A stuck controller shows up as EP0 disarmed, or as a state other than "configured"
            // (e.g. "not attached" after a spurious disconnect) that doesn't go away.
            let state = udc_state(&udc);
            let ep0_armed = m.read(DOEPCTL0) & DXEPCTL_EPENA != 0;
            let limit = if state == "configured" { 6 } else { 20 }; // 1.5 s / 5 s
            if state == "configured" && ep0_armed {
                bad = 0;
            } else {
                bad += 1;
                if bad >= limit {
                    if rebind(&udc) {
                        kmsg(&format!("gadget unhealthy (state {state}, ep0 armed {ep0_armed}), re-bound"));
                    }
                    bad = 0;
                    cooldown = tick + 40 * check_every; // ~10 s
                }
            }
        }
        thread::sleep(Duration::from_millis(interval_ms));
    }
}

/// Hardware watchdog keeper: if userspace or the kernel hangs, the SoC resets after `timeout` s.
fn watchdog(timeout: i32, interval: u64) -> ! {
    const WDIOC_SETTIMEOUT: i32 = 0xC004_5706u32 as i32; // _IOWR('W', 6, int)
    let mut f = OpenOptions::new()
        .write(true)
        .open("/dev/watchdog")
        .unwrap_or_else(|e| die(&format!("open /dev/watchdog: {e}")));
    let mut t = timeout;
    if unsafe { ioctl(f.as_raw_fd(), WDIOC_SETTIMEOUT, &mut t as *mut i32) } != 0 {
        kmsg(&format!("watchdog: set timeout {timeout}s failed, using driver default"));
    }
    kmsg(&format!("watchdog running, timeout {t}s, interval {interval}s"));
    loop {
        let _ = f.write_all(b"\0");
        thread::sleep(Duration::from_secs(interval));
    }
}

/// Read one register of an I2C device even if a kernel driver owns it (I2C_SLAVE_FORCE).
fn i2c_read(bus: usize, addr: u16, reg: u8) -> u8 {
    const I2C_SLAVE_FORCE: i32 = 0x0706;
    let mut f = OpenOptions::new()
        .read(true)
        .write(true)
        .open(format!("/dev/i2c-{bus}"))
        .unwrap_or_else(|e| die(&format!("open /dev/i2c-{bus}: {e}")));
    if unsafe { ioctl(f.as_raw_fd(), I2C_SLAVE_FORCE, addr as u32) } != 0 {
        die(&format!("I2C_SLAVE_FORCE {addr:#x}: {}", std::io::Error::last_os_error()));
    }
    let mut buf = [reg];
    f.write_all(&buf).unwrap_or_else(|e| die(&format!("write reg: {e}")));
    std::io::Read::read_exact(&mut f, &mut buf).unwrap_or_else(|e| die(&format!("read reg: {e}")));
    buf[0]
}

fn reboot(mode: &str) -> ! {
    const SYS_REBOOT: i32 = 88;
    const MAGIC1: u32 = 0xfee1_dead;
    const MAGIC2: u32 = 672_274_793;
    const CMD_RESTART2: u32 = 0xa1b2_c3d4;
    let arg = CString::new(mode).unwrap();
    kmsg(&format!("reboot {mode}"));
    unsafe {
        sync();
        syscall(SYS_REBOOT, MAGIC1, MAGIC2, CMD_RESTART2, arg.as_ptr());
    }
    die(&format!("reboot failed: {}", std::io::Error::last_os_error()))
}

fn main() {
    let args: Vec<String> = env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("peek") if args.len() == 3 => {
            let (page, off) = split(parse(&args[2]));
            println!("{:#010x}", Mmio::map(page).read(off));
        }
        Some("poke") if args.len() == 4 => {
            let (page, off) = split(parse(&args[2]));
            Mmio::map(page).write(off, parse(&args[3]) as u32);
        }
        Some("usbfix") => usbfix(args.get(2).map_or(20, |s| parse(s) as u64)),
        Some("i2cget") if args.len() == 5 => {
            let (bus, addr, reg) = (parse(&args[2]), parse(&args[3]), parse(&args[4]));
            println!("{:#04x}", i2c_read(bus, addr as u16, reg as u8));
        }
        Some("reboot") if args.len() == 3 => reboot(&args[2]),
        Some("watchdog") if args.len() == 4 => watchdog(parse(&args[2]) as i32, parse(&args[3]) as u64),
        _ => die("usage: peek <addr> | poke <addr> <val> | usbfix [ms] | i2cget <bus> <addr> <reg> | reboot <mode> | watchdog <timeout> <interval>"),
    }
}
