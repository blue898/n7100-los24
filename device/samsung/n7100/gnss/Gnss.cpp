#define LOG_TAG "gnss-n7100"

#include "Gnss.h"

#include <android-base/logging.h>
#include <hardware/hardware.h>
#include <utils/SystemClock.h>

#include <pthread.h>

namespace aidl::android::hardware::gnss::n7100 {

using ndk::ScopedAStatus;

std::mutex Gnss::sLock;
std::shared_ptr<IGnssCallback> Gnss::sCallback;
std::atomic<bool> Gnss::sSvStatus{true}, Gnss::sNmea{true};

static GpsCallbacks sLegacyCallbacks = {
        .size = sizeof(GpsCallbacks),
        .location_cb = Gnss::onLocation,
        .status_cb = Gnss::onStatus,
        .sv_status_cb = Gnss::onSvStatus,
        .nmea_cb = Gnss::onNmea,
        .set_capabilities_cb = Gnss::onCapabilities,
        .acquire_wakelock_cb = Gnss::onAcquireWakelock,
        .release_wakelock_cb = Gnss::onReleaseWakelock,
        .create_thread_cb = Gnss::onCreateThread,
        .request_utc_time_cb = Gnss::onRequestUtcTime,
        .set_system_info_cb = Gnss::onSystemInfo,
        .gnss_sv_status_cb = Gnss::onGnssSvStatus,
};

static ScopedAStatus unsupported() {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

Gnss::Gnss() {
    const hw_module_t* module;
    if (hw_get_module(GPS_HARDWARE_MODULE_ID, &module)) {
        LOG(ERROR) << "no legacy GPS HAL";
        return;
    }
    hw_device_t* device;
    if (module->methods->open(module, GPS_HARDWARE_MODULE_ID, &device)) {
        LOG(ERROR) << "legacy GPS HAL won't open";
        return;
    }
    mGps = reinterpret_cast<gps_device_t*>(device)->get_gps_interface(
            reinterpret_cast<gps_device_t*>(device));
    LOG(INFO) << "legacy GPS HAL: " << (mGps ? "ok" : "no interface");
}

std::shared_ptr<IGnssCallback> Gnss::callback() {
    std::lock_guard lk(sLock);
    return sCallback;
}

ScopedAStatus Gnss::setCallback(const std::shared_ptr<IGnssCallback>& callback) {
    {
        std::lock_guard lk(sLock);
        sCallback = callback;
    }
    if (!mGps) return unsupported();
    if (!mInitialized) {
        if (mGps->init(&sLegacyCallbacks)) {
            LOG(ERROR) << "legacy GPS HAL init failed";
            return ScopedAStatus::fromServiceSpecificError(-1);
        }
        mInitialized = true;
    }
    if (callback) {
        callback->gnssSetSystemInfoCb({.yearOfHw = 2012, .name = "BCM4752 (Samsung gpsd)"});
        callback->gnssSetCapabilitiesCb(IGnssCallback::CAPABILITY_SCHEDULING |
                                        IGnssCallback::CAPABILITY_ON_DEMAND_TIME);
    }
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::start() {
    return mGps && !mGps->start() ? ScopedAStatus::ok() : ScopedAStatus::fromServiceSpecificError(-1);
}

ScopedAStatus Gnss::stop() {
    return mGps && !mGps->stop() ? ScopedAStatus::ok() : ScopedAStatus::fromServiceSpecificError(-1);
}

ScopedAStatus Gnss::close() {
    if (mGps && mInitialized) mGps->cleanup();
    mInitialized = false;
    std::lock_guard lk(sLock);
    sCallback = nullptr;
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::injectTime(int64_t timeMs, int64_t timeReferenceMs, int uncertaintyMs) {
    if (mGps) mGps->inject_time(timeMs, timeReferenceMs, uncertaintyMs);
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::injectLocation(const GnssLocation& location) {
    if (mGps)
        mGps->inject_location(location.latitudeDegrees, location.longitudeDegrees,
                              location.horizontalAccuracyMeters);
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::injectBestLocation(const GnssLocation& location) {
    return injectLocation(location);
}

// the AIDL flags are the legacy GPS_DELETE_* values
ScopedAStatus Gnss::deleteAidingData(GnssAidingData aidingDataFlags) {
    if (mGps) mGps->delete_aiding_data(static_cast<GpsAidingData>(aidingDataFlags));
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::setPositionMode(const PositionModeOptions& o) {
    if (!mGps) return unsupported();
    int err = mGps->set_position_mode(static_cast<GpsPositionMode>(o.mode),
                                      static_cast<GpsPositionRecurrence>(o.recurrence),
                                      o.minIntervalMs, o.preferredAccuracyMeters,
                                      o.preferredTimeMs);
    return err ? ScopedAStatus::fromServiceSpecificError(err) : ScopedAStatus::ok();
}

ScopedAStatus Gnss::startSvStatus() {
    sSvStatus = true;
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::stopSvStatus() {
    sSvStatus = false;
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::startNmea() {
    sNmea = true;
    return ScopedAStatus::ok();
}

ScopedAStatus Gnss::stopNmea() {
    sNmea = false;
    return ScopedAStatus::ok();
}

/* extensions: not available (the framework then disables the feature) */
ScopedAStatus Gnss::getExtensionPsds(std::shared_ptr<IGnssPsds>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssConfiguration(std::shared_ptr<IGnssConfiguration>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssPowerIndication(std::shared_ptr<IGnssPowerIndication>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssMeasurement(std::shared_ptr<IGnssMeasurementInterface>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssBatching(std::shared_ptr<IGnssBatching>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssNavigationMessage(
        std::shared_ptr<IGnssNavigationMessageInterface>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionAGnss(std::shared_ptr<IAGnss>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionAGnssRil(std::shared_ptr<IAGnssRil>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssDebug(std::shared_ptr<IGnssDebug>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssVisibilityControl(
        std::shared_ptr<visibility_control::IGnssVisibilityControl>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionMeasurementCorrections(
        std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>* out) {
    *out = nullptr;
    return unsupported();
}
ScopedAStatus Gnss::getExtensionGnssAssistanceInterface(
        std::shared_ptr<gnss_assistance::IGnssAssistanceInterface>* out) {
    *out = nullptr;
    return unsupported();
}

/* --- legacy callbacks --- */

void Gnss::onLocation(GpsLocation* l) {
    auto cb = callback();
    if (!cb || !l) return;
    GnssLocation loc{};
    // the legacy GPS_LOCATION_HAS_* bits match the AIDL ones up to the (horizontal) accuracy
    loc.gnssLocationFlags = l->flags & 0x1f;
    loc.latitudeDegrees = l->latitude;
    loc.longitudeDegrees = l->longitude;
    loc.altitudeMeters = l->altitude;
    loc.speedMetersPerSec = l->speed;
    loc.bearingDegrees = l->bearing;
    loc.horizontalAccuracyMeters = l->accuracy;
    loc.timestampMillis = l->timestamp;
    loc.elapsedRealtime = {.flags = ElapsedRealtime::HAS_TIMESTAMP_NS |
                                    ElapsedRealtime::HAS_TIME_UNCERTAINTY_NS,
                           .timestampNs = ::android::elapsedRealtimeNano(),
                           .timeUncertaintyNs = 1000000.0};
    cb->gnssLocationCb(loc);
}

void Gnss::onStatus(GpsStatus* status) {
    auto cb = callback();
    if (cb && status)
        cb->gnssStatusCb(static_cast<IGnssCallback::GnssStatusValue>(status->status));
}

// Broadcom numbering: GPS 1-32, SBAS 33-64 (+87 = PRN 120-151), GLONASS 65-96
static void toSv(int prn, IGnssCallback::GnssSvInfo& sv) {
    if (prn >= 1 && prn <= 32) {
        sv.svid = prn;
        sv.constellation = GnssConstellationType::GPS;
        sv.carrierFrequencyHz = 1575420000;
    } else if (prn >= 33 && prn <= 64) {
        sv.svid = prn + 87;
        sv.constellation = GnssConstellationType::SBAS;
        sv.carrierFrequencyHz = 1575420000;
    } else if (prn >= 65 && prn <= 96) {
        sv.svid = prn - 64;
        sv.constellation = GnssConstellationType::GLONASS;
        sv.carrierFrequencyHz = 1602000000;
    } else {
        sv.svid = prn;
        sv.constellation = GnssConstellationType::UNKNOWN;
    }
}

void Gnss::onSvStatus(GpsSvStatus* s) {
    auto cb = callback();
    if (!cb || !s || !sSvStatus) return;
    std::vector<IGnssCallback::GnssSvInfo> list;
    for (int i = 0; i < s->num_svs && i < GPS_MAX_SVS; i++) {
        const GpsSvInfo& in = s->sv_list[i];
        IGnssCallback::GnssSvInfo sv{};
        toSv(in.prn, sv);
        sv.cN0Dbhz = in.snr;
        sv.elevationDegrees = in.elevation;
        sv.azimuthDegrees = in.azimuth;
        using F = IGnssCallback::GnssSvFlags;
        int flags = sv.carrierFrequencyHz ? static_cast<int>(F::HAS_CARRIER_FREQUENCY) : 0;
        if (in.prn >= 1 && in.prn <= 32) {
            uint32_t bit = 1u << (in.prn - 1);
            if (s->ephemeris_mask & bit) flags |= static_cast<int>(F::HAS_EPHEMERIS_DATA);
            if (s->almanac_mask & bit) flags |= static_cast<int>(F::HAS_ALMANAC_DATA);
            if (s->used_in_fix_mask & bit) flags |= static_cast<int>(F::USED_IN_FIX);
        }
        sv.svFlag = flags;
        list.push_back(sv);
    }
    cb->gnssSvStatusCb(list);
}

void Gnss::onGnssSvStatus(GnssSvStatus* s) {
    auto cb = callback();
    if (!cb || !s || !sSvStatus) return;
    std::vector<IGnssCallback::GnssSvInfo> list;
    for (int i = 0; i < s->num_svs && i < GNSS_MAX_SVS; i++) {
        const GnssSvInfo& in = s->gnss_sv_list[i];
        IGnssCallback::GnssSvInfo sv{};
        sv.svid = in.svid;
        sv.constellation = static_cast<GnssConstellationType>(in.constellation);
        sv.cN0Dbhz = in.c_n0_dbhz;
        sv.elevationDegrees = in.elevation;
        sv.azimuthDegrees = in.azimuth;
        sv.svFlag = in.flags;
        list.push_back(sv);
    }
    cb->gnssSvStatusCb(list);
}

void Gnss::onNmea(GpsUtcTime timestamp, const char* nmea, int length) {
    auto cb = callback();
    if (cb && nmea && sNmea) cb->gnssNmeaCb(timestamp, std::string(nmea, length));
}

void Gnss::onCapabilities(uint32_t capabilities) {
    auto cb = callback();
    // legacy and AIDL agree on scheduling/MSB/MSA/single shot/on-demand time; no assistance here
    if (cb) cb->gnssSetCapabilitiesCb(capabilities & (IGnssCallback::CAPABILITY_SCHEDULING |
                                                      IGnssCallback::CAPABILITY_SINGLE_SHOT |
                                                      IGnssCallback::CAPABILITY_ON_DEMAND_TIME));
}

void Gnss::onAcquireWakelock() {
    if (auto cb = callback()) cb->gnssAcquireWakelockCb();
}

void Gnss::onReleaseWakelock() {
    if (auto cb = callback()) cb->gnssReleaseWakelockCb();
}

pthread_t Gnss::onCreateThread(const char* name, void (*start)(void*), void* arg) {
    pthread_t thread;
    if (pthread_create(&thread, nullptr, reinterpret_cast<void* (*)(void*)>(start), arg)) return 0;
    pthread_setname_np(thread, name);
    return thread;
}

void Gnss::onRequestUtcTime() {
    if (auto cb = callback()) cb->gnssRequestTimeCb();
}

void Gnss::onSystemInfo(const GnssSystemInfo* info) {
    auto cb = callback();
    if (cb && info) cb->gnssSetSystemInfoCb({.yearOfHw = info->year_of_hw, .name = "BCM4752"});
}

}  // namespace aidl::android::hardware::gnss::n7100
