#include "Booster.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include <algorithm>

namespace n7100power {

using namespace std::chrono_literals;
using android::base::ReadFileToString;
using android::base::Split;
using android::base::Trim;
using android::base::WriteStringToFile;

static constexpr char kCpu[] = "/sys/devices/system/cpu/cpufreq/policy0/";
static constexpr char kGpu[] = "/sys/class/devfreq/13000000.gpu/";
static constexpr char kDmc[] = "/sys/class/devfreq/bus-dmc/";

static std::vector<long> readSteps(const std::string& path) {
    std::string s;
    std::vector<long> steps;
    if (!ReadFileToString(path, &s)) return steps;
    for (const auto& w : Split(Trim(s), " "))
        if (!w.empty()) steps.push_back(std::stol(w));
    std::sort(steps.begin(), steps.end());
    return steps;
}

static long readLong(const std::string& path, long def) {
    std::string s;
    return ReadFileToString(path, &s) ? std::stol(Trim(s)) : def;
}

Booster::Booster() {
    mCpu = {std::string(kCpu) + "scaling_min_freq", std::string(kCpu) + "scaling_max_freq",
            readSteps(std::string(kCpu) + "scaling_available_frequencies"), 0};
    // without boost, the 1.7 GHz turbo OPP isn't in the list; cpuinfo_max_freq would be
    mCpu.top = mCpu.steps.empty() ? readLong(std::string(kCpu) + "cpuinfo_max_freq", 0)
                                  : mCpu.steps.back();
    mGpu = {std::string(kGpu) + "min_freq", std::string(kGpu) + "max_freq",
            readSteps(std::string(kGpu) + "available_frequencies"), 0};
    mGpu.top = mGpu.steps.empty() ? 0 : mGpu.steps.back();
    mDmc = {std::string(kDmc) + "min_freq", std::string(kDmc) + "max_freq",
            readSteps(std::string(kDmc) + "available_frequencies"), 0};
    mDmc.top = mDmc.steps.empty() ? 0 : mDmc.steps.back();
    LOG(INFO) << "cpu " << mCpu.steps.size() << " steps (top " << mCpu.top << "), gpu "
              << mGpu.steps.size() << ", dmc " << mDmc.steps.size();
    mThread = std::thread(&Booster::loop, this);
}

// the lowest step >= wanted (the top one if none)
long Booster::pick(const Domain& d, long wanted) {
    for (long s : d.steps)
        if (s >= wanted) return s;
    return d.top;
}

void Booster::boost(Level level, std::chrono::milliseconds duration) {
    std::lock_guard lk(mLock);
    auto until = std::chrono::steady_clock::now() + duration;
    auto& t = mTimed[level];
    t = std::max(t, until);
    apply();
    mCv.notify_one();
}

void Booster::hold(const std::string& name, Level level, bool on) {
    std::lock_guard lk(mLock);
    if (on)
        mHeld[name] = level;
    else
        mHeld.erase(name);
    apply();
}

void Booster::releaseAll() {
    std::lock_guard lk(mLock);
    mHeld.clear();
    mTimed.clear();
    apply();
}

void Booster::setLowPower(bool on) {
    std::lock_guard lk(mLock);
    mLowPower = on;
    apply();
}

void Booster::loop() {
    std::unique_lock lk(mLock);
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        auto next = std::chrono::steady_clock::time_point::max();
        bool expired = false;
        for (auto it = mTimed.begin(); it != mTimed.end();) {
            if (it->second <= now) {
                it = mTimed.erase(it);
                expired = true;
            } else {
                next = std::min(next, it->second);
                ++it;
            }
        }
        if (expired) apply();
        if (next == std::chrono::steady_clock::time_point::max())
            mCv.wait(lk);
        else
            mCv.wait_until(lk, next);
    }
}

void Booster::apply() {
    Level level = Level::NONE;
    for (const auto& [l, until] : mTimed) level = std::max(level, l);
    for (const auto& [n, l] : mHeld) level = std::max(level, l);
    if (level == mApplied && mLowPower == mAppliedLowPower) return;

    // caps first (battery saver: 1 GHz CPU, GPU one step below the top), then floors,
    // so that a floor never ends up above its cap
    long cpuMax = mLowPower ? pick(mCpu, 1000000) : mCpu.top;
    long gpuMax = mLowPower && mGpu.steps.size() > 1 ? mGpu.steps[mGpu.steps.size() - 2] : mGpu.top;
    long cpuMin, gpuMin, dmcMin;
    switch (level) {
        case Level::LAUNCH:
            cpuMin = cpuMax;
            gpuMin = gpuMax;
            dmcMin = mDmc.top;
            break;
        case Level::INTERACTION:
            cpuMin = std::min(pick(mCpu, 1000000), cpuMax);
            gpuMin = std::min(pick(mGpu, 267000000), gpuMax);
            dmcMin = pick(mDmc, 267000000);
            break;
        default:
            cpuMin = mCpu.steps.empty() ? 0 : mCpu.steps.front();
            gpuMin = mGpu.steps.empty() ? 0 : mGpu.steps.front();
            dmcMin = mDmc.steps.empty() ? 0 : mDmc.steps.front();
            break;
    }

    auto write = [](const std::string& path, long v) {
        if (v && !WriteStringToFile(std::to_string(v), path))
            PLOG(WARNING) << "write " << v << " to " << path;
    };
    // keep min <= max at every single write
    write(mCpu.minPath, std::min(cpuMin, readLong(mCpu.maxPath, cpuMax)));
    write(mCpu.maxPath, cpuMax);
    write(mCpu.minPath, cpuMin);
    write(mGpu.minPath, std::min(gpuMin, readLong(mGpu.maxPath, gpuMax)));
    write(mGpu.maxPath, gpuMax);
    write(mGpu.minPath, gpuMin);
    write(mDmc.minPath, dmcMin);

    mApplied = level;
    mAppliedLowPower = mLowPower;
}

}  // namespace n7100power
