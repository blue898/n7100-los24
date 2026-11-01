/*
 * Frequency floors and caps behind the power HAL's boosts and modes, for the
 * Exynos 4412 Prime of the Galaxy Note II: one cpufreq policy for all four
 * Cortex-A9s, devfreq for the Mali-400 and the memory bus (DMC).
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace n7100power {

enum class Level {
    NONE,         // let schedutil and the devfreq governors decide
    INTERACTION,  // touch, scrolling: no ramp-up from the lowest steps
    LAUNCH,       // app launch, camera: everything at the top
};

class Booster {
  public:
    Booster();
    // floor for `duration`, then back (overlapping requests keep the highest)
    void boost(Level level, std::chrono::milliseconds duration);
    // floor while `on` (a mode); `name` identifies the holder
    void hold(const std::string& name, Level level, bool on);
    void releaseAll();
    void setLowPower(bool on);

  private:
    struct Domain {
        std::string minPath, maxPath;
        std::vector<long> steps;  // ascending, sysfs units
        long top;                 // highest step without boost/turbo
    };
    void loop();
    void apply();  // called with mLock held
    static long pick(const Domain& d, long wanted);

    Domain mCpu, mGpu, mDmc;
    std::mutex mLock;
    std::condition_variable mCv;
    std::map<Level, std::chrono::steady_clock::time_point> mTimed;
    std::map<std::string, Level> mHeld;
    bool mLowPower = false;
    Level mApplied = Level::NONE;
    bool mAppliedLowPower = false;
    std::thread mThread;
};

}  // namespace n7100power
