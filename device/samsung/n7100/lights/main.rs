//! ILights for the Galaxy Note II on mainline Linux.
//!
//! BUTTONS drives the capacitive key backlight, NOTIFICATIONS/BATTERY/ATTENTION share the RGB
//! status LED (highest priority wins). The LED controller (AN30259A) flashes by itself through the
//! timer trigger, so the LED keeps flashing in deep sleep; it knows 500 ms steps up to 7.5 s.

use std::fmt::Display;
use std::fs;
use std::io::ErrorKind;
use std::sync::Mutex;
use std::thread;
use std::time::Duration;

use android_hardware_light::aidl::android::hardware::light::{
    FlashMode::FlashMode, HwLight::HwLight, HwLightEffect::HwLightEffect, HwLightState::HwLightState,
    ILights::{BnLights, ILights},
    LightType::LightType,
};
use binder::{BinderFeatures, ExceptionCode, Interface, Status};
use log::{error, LevelFilter};

const LOG_TAG: &str = "lights-n7100";
const BUTTONS: &str = "/sys/class/leds/tm2-touchkey/brightness";
const RGB: [&str; 3] = ["red:status", "green:status", "blue:status"];
// LED current for a full colour channel, of 255: Samsung's driver used 0x28, the full current is
// glaring
const RGB_MAX: u32 = 0x28;
const BLINK_STEP_MS: i32 = 500;
const BLINK_MAX_MS: i32 = 7500;

// light ids; the RGB ones are listed lowest priority first
const ID_BUTTONS: i32 = 0;
const ID_BATTERY: i32 = 1;
const ID_NOTIFICATIONS: i32 = 2;
const ID_ATTENTION: i32 = 3;
const RGB_IDS: [i32; 3] = [ID_BATTERY, ID_NOTIFICATIONS, ID_ATTENTION];

fn write(path: &str, value: impl Display) {
    if let Err(e) = fs::write(path, value.to_string()) {
        error!("write {path}: {e}");
    }
}

// The timer trigger creates its delay files when it is set; ueventd hands them to us on the
// trigger's change uevent, which takes a moment.
fn write_delay(path: &str, ms: i32) {
    for _ in 0..50 {
        match fs::write(path, ms.to_string()) {
            Err(e) if matches!(e.kind(), ErrorKind::PermissionDenied | ErrorKind::NotFound) => {
                thread::sleep(Duration::from_millis(10))
            }
            Err(e) => return error!("write {path}: {e}"),
            Ok(()) => return,
        }
    }
    error!("write {path}: not writable");
}

fn blink_ms(ms: i32) -> i32 {
    ((ms + BLINK_STEP_MS / 2) / BLINK_STEP_MS * BLINK_STEP_MS).clamp(BLINK_STEP_MS, BLINK_MAX_MS)
}

fn max_brightness(led: &str) -> u32 {
    fs::read_to_string(format!("/sys/class/leds/{led}/max_brightness"))
        .ok()
        .and_then(|s| s.trim().parse().ok())
        .unwrap_or(255)
}

fn set_rgb(color: i32, blink: Option<(i32, i32)>) {
    for (i, led) in RGB.iter().enumerate() {
        let dir = format!("/sys/class/leds/{led}");
        let c = ((color as u32) >> (16 - 8 * i)) & 0xff;
        // leaving the timer trigger turns the LED off
        write(&format!("{dir}/trigger"), "none");
        // rounded up so that dimmed colours stay lit
        write(&format!("{dir}/brightness"), (c * RGB_MAX).div_ceil(255));
        if let (Some((on, off)), true) = (blink, c != 0) {
            write(&format!("{dir}/trigger"), "timer");
            write_delay(&format!("{dir}/delay_off"), off);
            write_delay(&format!("{dir}/delay_on"), on);
        }
    }
}

fn lit(state: &HwLightState) -> bool {
    state.color & 0x00ff_ffff != 0
}

struct Lights {
    rgb: Mutex<[HwLightState; 3]>,
}

impl Interface for Lights {}

impl Lights {
    fn update_rgb(&self) {
        let states = self.rgb.lock().unwrap();
        let Some(state) = states.iter().rev().find(|s| lit(s)) else {
            return set_rgb(0, None);
        };
        if state.flashMode == FlashMode::NONE || state.flashOnMs <= 0 || state.flashOffMs <= 0 {
            return set_rgb(state.color, None);
        }
        set_rgb(state.color, Some((blink_ms(state.flashOnMs), blink_ms(state.flashOffMs))));
    }
}

impl ILights for Lights {
    fn setLightState(&self, id: i32, state: &HwLightState) -> binder::Result<()> {
        if id == ID_BUTTONS {
            write(BUTTONS, if lit(state) { max_brightness("tm2-touchkey") } else { 0 });
            return Ok(());
        }
        let Some(slot) = RGB_IDS.iter().position(|&i| i == id) else {
            return Err(Status::new_exception(ExceptionCode::UNSUPPORTED_OPERATION, None));
        };
        self.rgb.lock().unwrap()[slot] = *state;
        self.update_rgb();
        Ok(())
    }

    fn setLightEffects(&self, _effects: &[HwLightEffect]) -> binder::Result<()> {
        Err(Status::new_exception(ExceptionCode::UNSUPPORTED_OPERATION, None))
    }

    fn getLights(&self) -> binder::Result<Vec<HwLight>> {
        let light = |id: i32, r#type: LightType| HwLight { id, ordinal: 0, r#type, minUpdatePeriodMillis: 0 };
        Ok(vec![
            light(ID_BUTTONS, LightType::BUTTONS),
            light(ID_BATTERY, LightType::BATTERY),
            light(ID_NOTIFICATIONS, LightType::NOTIFICATIONS),
            light(ID_ATTENTION, LightType::ATTENTION),
        ])
    }
}

fn main() {
    let _ = logger::init(logger::Config::default().with_tag_on_device(LOG_TAG).with_max_level(LevelFilter::Info));
    binder::ProcessState::set_thread_pool_max_thread_count(0);
    let lights = Lights { rgb: Mutex::new(Default::default()) };
    let binder = BnLights::new_binder(lights, BinderFeatures::default());
    let name = format!("{}/default", Lights::get_descriptor());
    binder::add_service(&name, binder.as_binder()).expect("Failed to register service");
    binder::ProcessState::join_thread_pool()
}
