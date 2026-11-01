//! ILights for the Galaxy Note II on mainline Linux.
//!
//! BUTTONS drives the capacitive key backlight, NOTIFICATIONS/BATTERY/ATTENTION share the RGB
//! status LED (highest priority wins). The LED driver has no usable hardware blink without
//! triggers whose sysfs files ueventd can't chown, so timed flashing runs in a thread here.

use std::fs;
use std::sync::{Arc, Mutex};
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

// light ids; the RGB ones are listed lowest priority first
const ID_BUTTONS: i32 = 0;
const ID_BATTERY: i32 = 1;
const ID_NOTIFICATIONS: i32 = 2;
const ID_ATTENTION: i32 = 3;
const RGB_IDS: [i32; 3] = [ID_BATTERY, ID_NOTIFICATIONS, ID_ATTENTION];

fn write(path: &str, value: u32) {
    if let Err(e) = fs::write(path, value.to_string()) {
        error!("write {path}: {e}");
    }
}

fn max_brightness(led: &str) -> u32 {
    fs::read_to_string(format!("/sys/class/leds/{led}/max_brightness"))
        .ok()
        .and_then(|s| s.trim().parse().ok())
        .unwrap_or(255)
}

fn set_rgb(color: i32) {
    for (i, led) in RGB.iter().enumerate() {
        let c = ((color as u32) >> (16 - 8 * i)) & 0xff;
        write(&format!("/sys/class/leds/{led}/brightness"), c * max_brightness(led) / 255);
    }
}

fn lit(state: &HwLightState) -> bool {
    state.color & 0x00ff_ffff != 0
}

#[derive(Default)]
struct Rgb {
    states: [HwLightState; 3],
    // bumped on every change so a running blink thread knows it is stale
    generation: u64,
}

struct Lights {
    rgb: Arc<Mutex<Rgb>>,
}

impl Interface for Lights {}

impl Lights {
    fn update_rgb(&self) {
        let mut rgb = self.rgb.lock().unwrap();
        rgb.generation += 1;
        let Some(state) = rgb.states.iter().rev().find(|s| lit(s)).copied() else {
            set_rgb(0);
            return;
        };
        if state.flashMode == FlashMode::NONE || state.flashOnMs <= 0 || state.flashOffMs <= 0 {
            set_rgb(state.color);
            return;
        }
        let generation = rgb.generation;
        let shared = Arc::clone(&self.rgb);
        let (on, off) = (state.flashOnMs as u64, state.flashOffMs as u64);
        thread::spawn(move || loop {
            for (color, ms) in [(state.color, on), (0, off)] {
                if shared.lock().unwrap().generation != generation {
                    return;
                }
                set_rgb(color);
                thread::sleep(Duration::from_millis(ms));
            }
        });
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
        self.rgb.lock().unwrap().states[slot] = *state;
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
    let lights = Lights { rgb: Arc::new(Mutex::new(Rgb::default())) };
    let binder = BnLights::new_binder(lights, BinderFeatures::default());
    let name = format!("{}/default", Lights::get_descriptor());
    binder::add_service(&name, binder.as_binder()).expect("Failed to register service");
    binder::ProcessState::join_thread_pool()
}
