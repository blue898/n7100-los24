/*
 * GNSS HAL for the Galaxy Note II: Samsung's legacy GPS HAL (gps.default.so, a client of
 * Broadcom's gpsd for the BCM4752) behind the AIDL IGnss of current Android.
 */
#pragma once

#include <aidl/android/hardware/gnss/BnGnss.h>
#include <hardware/gps.h>

#include <atomic>
#include <mutex>

namespace aidl::android::hardware::gnss::n7100 {

class Gnss : public BnGnss {
  public:
    Gnss();

    ndk::ScopedAStatus setCallback(const std::shared_ptr<IGnssCallback>& callback) override;
    ndk::ScopedAStatus start() override;
    ndk::ScopedAStatus stop() override;
    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus injectTime(int64_t timeMs, int64_t timeReferenceMs,
                                  int uncertaintyMs) override;
    ndk::ScopedAStatus injectLocation(const GnssLocation& location) override;
    ndk::ScopedAStatus injectBestLocation(const GnssLocation& location) override;
    ndk::ScopedAStatus deleteAidingData(GnssAidingData aidingDataFlags) override;
    ndk::ScopedAStatus setPositionMode(const PositionModeOptions& options) override;
    ndk::ScopedAStatus startSvStatus() override;
    ndk::ScopedAStatus stopSvStatus() override;
    ndk::ScopedAStatus startNmea() override;
    ndk::ScopedAStatus stopNmea() override;

    // no extensions: PSDS/SUPL/measurements need Broadcom's AGPS interfaces
    ndk::ScopedAStatus getExtensionPsds(std::shared_ptr<IGnssPsds>* out) override;
    ndk::ScopedAStatus getExtensionGnssConfiguration(
            std::shared_ptr<IGnssConfiguration>* out) override;
    ndk::ScopedAStatus getExtensionGnssPowerIndication(
            std::shared_ptr<IGnssPowerIndication>* out) override;
    ndk::ScopedAStatus getExtensionGnssMeasurement(
            std::shared_ptr<IGnssMeasurementInterface>* out) override;
    ndk::ScopedAStatus getExtensionGnssBatching(std::shared_ptr<IGnssBatching>* out) override;
    ndk::ScopedAStatus getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>* out) override;
    ndk::ScopedAStatus getExtensionGnssNavigationMessage(
            std::shared_ptr<IGnssNavigationMessageInterface>* out) override;
    ndk::ScopedAStatus getExtensionAGnss(std::shared_ptr<IAGnss>* out) override;
    ndk::ScopedAStatus getExtensionAGnssRil(std::shared_ptr<IAGnssRil>* out) override;
    ndk::ScopedAStatus getExtensionGnssDebug(std::shared_ptr<IGnssDebug>* out) override;
    ndk::ScopedAStatus getExtensionGnssVisibilityControl(
            std::shared_ptr<visibility_control::IGnssVisibilityControl>* out) override;
    ndk::ScopedAStatus getExtensionGnssAntennaInfo(
            std::shared_ptr<IGnssAntennaInfo>* out) override;
    ndk::ScopedAStatus getExtensionMeasurementCorrections(
            std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>* out)
            override;
    ndk::ScopedAStatus getExtensionGnssAssistanceInterface(
            std::shared_ptr<gnss_assistance::IGnssAssistanceInterface>* out) override;

    // legacy callbacks (on the HAL's threads)
    static void onLocation(GpsLocation* location);
    static void onStatus(GpsStatus* status);
    static void onSvStatus(GpsSvStatus* svStatus);
    static void onGnssSvStatus(GnssSvStatus* svStatus);
    static void onNmea(GpsUtcTime timestamp, const char* nmea, int length);
    static void onCapabilities(uint32_t capabilities);
    static void onAcquireWakelock();
    static void onReleaseWakelock();
    static pthread_t onCreateThread(const char* name, void (*start)(void*), void* arg);
    static void onRequestUtcTime();
    static void onSystemInfo(const GnssSystemInfo* info);

  private:
    static std::shared_ptr<IGnssCallback> callback();

    static std::mutex sLock;
    static std::shared_ptr<IGnssCallback> sCallback;
    static std::atomic<bool> sSvStatus, sNmea;
    const GpsInterface* mGps = nullptr;
    bool mInitialized = false;
};

}  // namespace aidl::android::hardware::gnss::n7100
