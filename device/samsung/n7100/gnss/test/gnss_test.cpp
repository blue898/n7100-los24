/*
 * gnss-test-n7100: drive the IGnss HAL from the shell and print what it reports, once a
 * second: satellites in view / used, the strongest C/N0, NMEA sentences and fixes (the
 * position itself only as accuracy, not as coordinates). Debugging aid, not installed by
 * default: m gnss-test-n7100 and push it to /vendor/bin.
 * It takes over the HAL's callback, so restart the HAL afterwards
 * (stop vendor.gnss-n7100; start vendor.gnss-n7100) for system_server to reconnect.
 * usage: gnss-test-n7100 [seconds] [-n]    -n: also print the NMEA GSV/GSA sentences
 *                                          (satellites, no position)
 */
#include <aidl/android/hardware/gnss/BnGnssCallback.h>
#include <aidl/android/hardware/gnss/IGnss.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <vector>

using namespace aidl::android::hardware::gnss;
using ndk::ScopedAStatus;

namespace {

std::mutex gLock;
std::vector<IGnssCallback::GnssSvInfo> gSvs;
int gLocations, gNmea;
bool gPrintNmea;
float gLastAccuracy = -1;

class Callback : public BnGnssCallback {
  public:
    ScopedAStatus gnssSetCapabilitiesCb(int caps) override {
        printf("capabilities %#x\n", caps);
        return ScopedAStatus::ok();
    }
    ScopedAStatus gnssStatusCb(GnssStatusValue status) override {
        printf("status %d\n", static_cast<int>(status));
        return ScopedAStatus::ok();
    }
    ScopedAStatus gnssSvStatusCb(const std::vector<GnssSvInfo>& svs) override {
        std::lock_guard<std::mutex> lock(gLock);
        gSvs = svs;
        return ScopedAStatus::ok();
    }
    ScopedAStatus gnssLocationCb(const GnssLocation& location) override {
        std::lock_guard<std::mutex> lock(gLock);
        gLocations++;
        gLastAccuracy = location.horizontalAccuracyMeters;
        return ScopedAStatus::ok();
    }
    ScopedAStatus gnssNmeaCb(int64_t, const std::string& nmea) override {
        std::lock_guard<std::mutex> lock(gLock);
        gNmea++;
        if (gPrintNmea && (nmea.find("GSV,") != std::string::npos ||
                           nmea.find("GSA,") != std::string::npos))
            printf("%s", nmea.c_str());
        return ScopedAStatus::ok();
    }
    ScopedAStatus gnssAcquireWakelockCb() override { return ScopedAStatus::ok(); }
    ScopedAStatus gnssReleaseWakelockCb() override { return ScopedAStatus::ok(); }
    ScopedAStatus gnssSetSystemInfoCb(const GnssSystemInfo& info) override {
        printf("system info: year %d, %s\n", info.yearOfHw, info.name.c_str());
        return ScopedAStatus::ok();
    }
    ScopedAStatus gnssRequestTimeCb() override { return ScopedAStatus::ok(); }
    ScopedAStatus gnssRequestLocationCb(bool, bool) override { return ScopedAStatus::ok(); }
    ScopedAStatus gnssSetSignalTypeCapabilitiesCb(const std::vector<GnssSignalType>&) override {
        return ScopedAStatus::ok();
    }
};

void check(const char* what, const ScopedAStatus& status) {
    if (!status.isOk()) printf("%s: %s\n", what, status.getDescription().c_str());
}

}  // namespace

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 60;
    gPrintNmea = argc > 2 && !strcmp(argv[2], "-n");
    ABinderProcess_startThreadPool();
    auto gnss = IGnss::fromBinder(ndk::SpAIBinder(
            AServiceManager_waitForService("android.hardware.gnss.IGnss/default")));
    if (!gnss) {
        printf("no IGnss service\n");
        return 1;
    }
    check("setCallback", gnss->setCallback(ndk::SharedRefBase::make<Callback>()));
    IGnss::PositionModeOptions options;
    options.mode = IGnss::GnssPositionMode::STANDALONE;
    options.recurrence = IGnss::GnssPositionRecurrence::RECURRENCE_PERIODIC;
    options.minIntervalMs = 1000;
    check("setPositionMode", gnss->setPositionMode(options));
    check("startSvStatus", gnss->startSvStatus());
    check("startNmea", gnss->startNmea());
    check("start", gnss->start());

    for (int t = 1; t <= seconds; t++) {
        sleep(1);
        std::lock_guard<std::mutex> lock(gLock);
        int used = 0;
        std::vector<float> cn0;
        for (const auto& sv : gSvs) {
            if (sv.svFlag & static_cast<int>(IGnssCallback::GnssSvFlags::USED_IN_FIX)) used++;
            cn0.push_back(sv.cN0Dbhz);
        }
        std::sort(cn0.rbegin(), cn0.rend());
        printf("%3ds  sv %2zu used %2d  cn0", t, gSvs.size(), used);
        for (size_t i = 0; i < cn0.size() && i < 6; i++) printf(" %4.1f", cn0[i]);
        printf("  nmea %d  fixes %d", gNmea, gLocations);
        if (gLocations) printf(" (accuracy %.0f m)", gLastAccuracy);
        printf("\n");
        fflush(stdout);
    }
    check("stop", gnss->stop());
    _exit(0);  // binder threads may still be in a callback: no static destructors
}
