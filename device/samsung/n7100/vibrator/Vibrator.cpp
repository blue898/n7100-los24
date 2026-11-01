/*
 * IVibrator for the Galaxy Note II on mainline Linux. The max77693 haptic driver registers an
 * ff-memless input device ("max77693-haptic"): one FF_RUMBLE effect, played for the requested
 * time, with the amplitude as rumble magnitude.
 */

#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>

using aidl::android::hardware::vibrator::BnVibrator;
using aidl::android::hardware::vibrator::CompositeEffect;
using aidl::android::hardware::vibrator::CompositePrimitive;
using aidl::android::hardware::vibrator::Effect;
using aidl::android::hardware::vibrator::EffectStrength;
using aidl::android::hardware::vibrator::IVibrator;
using aidl::android::hardware::vibrator::IVibratorCallback;
using android::base::unique_fd;
using ndk::ScopedAStatus;

namespace {

constexpr char kDeviceName[] = "max77693-haptic";

unique_fd openFfDevice() {
    for (int i = 0; i < 32; i++) {
        unique_fd fd(open(("/dev/input/event" + std::to_string(i)).c_str(), O_RDWR | O_CLOEXEC));
        char name[64] = {};
        if (fd >= 0 && ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
            std::string(name) == kDeviceName) {
            return fd;
        }
    }
    return {};
}

ScopedAStatus unsupported() {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

class Vibrator : public BnVibrator {
  public:
    explicit Vibrator(unique_fd fd) : fd_(std::move(fd)) {}

    ScopedAStatus getCapabilities(int32_t* caps) override {
        *caps = CAP_ON_CALLBACK | CAP_PERFORM_CALLBACK | CAP_AMPLITUDE_CONTROL;
        return ScopedAStatus::ok();
    }

    ScopedAStatus off() override {
        std::lock_guard lock(mutex_);
        generation_++;
        send(0);
        return ScopedAStatus::ok();
    }

    ScopedAStatus on(int32_t timeoutMs, const std::shared_ptr<IVibratorCallback>& cb) override {
        return start(timeoutMs, cb);
    }

    ScopedAStatus perform(Effect effect, EffectStrength strength,
                          const std::shared_ptr<IVibratorCallback>& cb, int32_t* ret) override {
        int32_t ms;
        switch (effect) {
            case Effect::TICK: ms = 10; break;
            case Effect::CLICK: ms = 20; break;
            case Effect::HEAVY_CLICK: ms = 30; break;
            case Effect::DOUBLE_CLICK: ms = 40; break;
            default: return unsupported();
        }
        if (strength == EffectStrength::LIGHT) ms = ms * 2 / 3;
        if (strength == EffectStrength::STRONG) ms = ms * 3 / 2;
        *ret = ms;
        return start(ms, cb);
    }

    ScopedAStatus getSupportedEffects(std::vector<Effect>* ret) override {
        *ret = {Effect::TICK, Effect::CLICK, Effect::HEAVY_CLICK, Effect::DOUBLE_CLICK};
        return ScopedAStatus::ok();
    }

    ScopedAStatus setAmplitude(float amplitude) override {
        if (amplitude <= 0.0f || amplitude > 1.0f) {
            return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        std::lock_guard lock(mutex_);
        magnitude_ = static_cast<uint16_t>(amplitude * 0xffff);
        return ScopedAStatus::ok();
    }

    ScopedAStatus setExternalControl(bool) override { return unsupported(); }
    ScopedAStatus getCompositionDelayMax(int32_t*) override { return unsupported(); }
    ScopedAStatus getCompositionSizeMax(int32_t*) override { return unsupported(); }
    ScopedAStatus getSupportedPrimitives(std::vector<CompositePrimitive>*) override {
        return unsupported();
    }
    ScopedAStatus getPrimitiveDuration(CompositePrimitive, int32_t*) override {
        return unsupported();
    }
    ScopedAStatus compose(const std::vector<CompositeEffect>&,
                          const std::shared_ptr<IVibratorCallback>&) override {
        return unsupported();
    }
    ScopedAStatus getSupportedAlwaysOnEffects(std::vector<Effect>* ret) override {
        ret->clear();
        return ScopedAStatus::ok();
    }
    ScopedAStatus alwaysOnEnable(int32_t, Effect, EffectStrength) override { return unsupported(); }
    ScopedAStatus alwaysOnDisable(int32_t) override { return unsupported(); }

  private:
    // Upload the rumble effect with the given length and start (value 1) or stop (0) it.
    // Called with mutex_ held.
    bool send(int value, int32_t ms = 0) {
        if (value) {
            ff_effect effect = {};
            effect.type = FF_RUMBLE;
            effect.id = effect_id_;
            effect.u.rumble.strong_magnitude = magnitude_;
            effect.replay.length = static_cast<uint16_t>(std::min(ms, 0xffff));
            if (ioctl(fd_, EVIOCSFF, &effect) < 0) {
                PLOG(ERROR) << "EVIOCSFF";
                return false;
            }
            effect_id_ = effect.id;
        }
        if (effect_id_ < 0) return true;
        input_event event = {.type = EV_FF, .code = static_cast<uint16_t>(effect_id_), .value = value};
        if (write(fd_, &event, sizeof(event)) != sizeof(event)) {
            PLOG(ERROR) << "write EV_FF";
            return false;
        }
        return true;
    }

    ScopedAStatus start(int32_t ms, const std::shared_ptr<IVibratorCallback>& cb) {
        uint64_t generation;
        {
            std::lock_guard lock(mutex_);
            generation = ++generation_;
            if (!send(1, ms)) return ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
        }
        if (cb) {
            std::thread([this, ms, cb, generation] {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                {
                    std::lock_guard lock(mutex_);
                    if (generation_ != generation) return;  // stopped or restarted meanwhile
                }
                cb->onComplete();
            }).detach();
        }
        return ScopedAStatus::ok();
    }

    unique_fd fd_;
    std::mutex mutex_;
    int effect_id_ = -1;
    uint16_t magnitude_ = 0xffff;
    uint64_t generation_ = 0;
};

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    unique_fd fd = openFfDevice();
    if (fd < 0) {
        LOG(FATAL) << "no " << kDeviceName << " input device";
    }
    auto vibrator = ndk::SharedRefBase::make<Vibrator>(std::move(fd));
    const std::string name = std::string(Vibrator::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(vibrator->asBinder().get(), name.c_str()), STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
