/*
 * Galaxy Note II sensors HAL, see Sensors.h.
 *
 * Based on the AOSP example HAL (hardware/interfaces/sensors/aidl/default):
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "sensors-n7100"

#include "Sensors.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fcntl.h>
#include <hardware_legacy/power.h>
#include <poll.h>
#include <unistd.h>
#include <utils/SystemClock.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace aidl::android::hardware::sensors {

using ::ndk::ScopedAStatus;
using EventPayload = Event::EventPayload;
using MetaDataEventType = EventPayload::MetaData::MetaDataEventType;

namespace {

constexpr const char* kHubDevice = "/dev/ssp_sensorhub";
constexpr const char* kMagAsa = "/sys/bus/i2c/devices/1-0018/mag_asa";
constexpr const char* kMagBiasFile = "/data/vendor/sensors/mag_bias";

constexpr float kGravity = 9.80665f;
constexpr float kAccelScale = kGravity / 1024.0f;                    // 1024 LSB/g
constexpr float kGyroScale = 17.5e-3f * static_cast<float>(M_PI) / 180.0f;  // 500 dps range
constexpr float kMagScale = 0.15f;                                   // uT/LSB, 16 bit
constexpr float kProxFar = 8.0f;                                     // cm

constexpr int64_t kMinHubPeriodNs = 10'000'000;
constexpr int64_t kMaxHubPeriodNs = 200'000'000;

// Hard-iron estimate: every axis must have been swept across this range (uT) first
constexpr float kMagCalSpan = 40.0f;

// Gyroscope at rest: no axis moves more than this (rad/s, ~0.9 dps) for this long
constexpr float kGyroStillSpan = 0.015f;
constexpr int64_t kGyroStillNs = 2'000'000'000;

}  // namespace

void GyroCalibration::update(const float v[3], int64_t timestamp) {
    if (mCount == 0) {
        mStart = timestamp;
        for (int i = 0; i < 3; i++) {
            mSum[i] = 0;
            mMin[i] = mMax[i] = v[i];
        }
    }
    bool still = true;
    for (int i = 0; i < 3; i++) {
        mSum[i] += v[i];
        mMin[i] = std::min(mMin[i], v[i]);
        mMax[i] = std::max(mMax[i], v[i]);
        still = still && mMax[i] - mMin[i] < kGyroStillSpan;
    }
    mCount++;
    if (!still) {
        mCount = 0;
        return;
    }
    if (timestamp - mStart < kGyroStillNs) return;
    for (int i = 0; i < 3; i++) bias[i] = mSum[i] / mCount;
    mCount = 0;
}

void MagCalibration::load() {
    std::string s;
    if (!::android::base::ReadFileToString(kMagBiasFile, &s)) return;
    if (sscanf(s.c_str(), "%f %f %f", &bias[0], &bias[1], &bias[2]) == 3) mValid = true;
}

void MagCalibration::save() {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f %.2f %.2f\n", bias[0], bias[1], bias[2]);
    if (!::android::base::WriteStringToFile(buf, kMagBiasFile))
        PLOG(WARNING) << "can't write " << kMagBiasFile;
}

int8_t MagCalibration::update(const float raw[3]) {
    bool swept = true;
    for (int i = 0; i < 3; i++) {
        mMin[i] = std::min(mMin[i], raw[i]);
        mMax[i] = std::max(mMax[i], raw[i]);
        swept = swept && mMax[i] - mMin[i] > kMagCalSpan;
    }
    if (!swept)
        return static_cast<int8_t>(mValid ? SensorStatus::ACCURACY_LOW
                                          : SensorStatus::UNRELIABLE);

    float moved = 0;
    for (int i = 0; i < 3; i++) {
        float b = (mMin[i] + mMax[i]) / 2;
        moved = std::max(moved, std::fabs(b - bias[i]));
        bias[i] = b;
    }
    mValid = true;
    int64_t now = ::android::elapsedRealtimeNano();
    if (moved > 1.0f && now - mLastSaveNs > 60'000'000'000) {
        save();
        mLastSaveNs = now;
    }
    return static_cast<int8_t>(SensorStatus::ACCURACY_MEDIUM);
}

void Sensors::addSensor(SensorType type, const char* name, const char* vendor, uint32_t ssp,
                        float maxRange, float resolution, float power, int32_t minDelayUs,
                        int32_t maxDelayUs, uint32_t flags) {
    HalSensor s;
    s.ssp = ssp;
    s.info.sensorHandle = static_cast<int32_t>(mSensors.size()) + 1;
    s.info.name = name;
    s.info.vendor = vendor;
    s.info.version = 1;
    s.info.type = type;
    s.info.typeAsString = "";
    s.info.maxRange = maxRange;
    s.info.resolution = resolution;
    s.info.power = power;
    s.info.minDelayUs = minDelayUs;
    s.info.maxDelayUs = maxDelayUs;
    s.info.fifoReservedEventCount = 0;
    s.info.fifoMaxEventCount = 0;
    s.info.requiredPermission = "";
    s.info.flags = flags;
    mSensors.push_back(s);
}

Sensors::Sensors() {
    // The driver probes asynchronously and the MCU takes a moment to boot: wait for it
    for (int i = 0; i < 100 && mFd < 0; i++) {
        mFd = open(kHubDevice, O_RDWR | O_CLOEXEC);
        if (mFd < 0) usleep(100'000);
    }
    if (mFd < 0) {
        PLOG(ERROR) << "can't open " << kHubDevice << ", no sensors";
        return;
    }

    std::string asa;
    unsigned int a[3];
    if (::android::base::ReadFileToString(kMagAsa, &asa) &&
        sscanf(asa.c_str(), "%u %u %u", &a[0], &a[1], &a[2]) == 3 && a[0] && a[1] && a[2]) {
        for (int i = 0; i < 3; i++) mMagAsa[i] = (static_cast<float>(a[i]) - 128) / 256 + 1;
    }

    std::lock_guard<std::mutex> lock(mSensorLock);
    const uint32_t continuous = 0;
    const uint32_t onChange = static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE);
    const uint32_t wakeUp = static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_WAKE_UP);
    addSensor(SensorType::ACCELEROMETER, "LSM330 Accelerometer", "STMicroelectronics",
              SSP_ACCEL, 2 * kGravity, kAccelScale, 0.23f, 10'000, 200'000, continuous);
    addSensor(SensorType::GYROSCOPE, "LSM330 Gyroscope", "STMicroelectronics", SSP_GYRO,
              500.0f * static_cast<float>(M_PI) / 180.0f, kGyroScale, 6.1f, 10'000, 200'000,
              continuous);
    addSensor(SensorType::MAGNETIC_FIELD, "AK8963C Magnetometer", "Asahi Kasei Microdevices",
              SSP_MAG, 4912.0f, kMagScale, 0.28f, 10'000, 200'000, continuous);
    addSensor(SensorType::MAGNETIC_FIELD_UNCALIBRATED, "AK8963C Magnetometer (uncalibrated)",
              "Asahi Kasei Microdevices", SSP_MAG, 4912.0f, kMagScale, 0.28f, 10'000, 200'000,
              continuous);
    addSensor(SensorType::PRESSURE, "BMP182 Barometer", "Bosch", SSP_PRESSURE, 1100.0f, 0.01f,
              0.06f, 20'000, 200'000, continuous);
    addSensor(SensorType::LIGHT, "CM36651 Light", "Capella Microsystems", SSP_LIGHT, 65535.0f,
              1.0f, 0.75f, 66'000, 200'000, onChange);
    addSensor(SensorType::PROXIMITY, "CM36651 Proximity", "Capella Microsystems", SSP_PROX,
              kProxFar, kProxFar, 0.75f, 0, 200'000, onChange | wakeUp);
    mMagCal.load();

    if (pipe2(mStopPipe, O_CLOEXEC) == 0) {
        mReadThread = std::thread([this] { readLoop(); });
    } else {
        PLOG(ERROR) << "pipe2";
    }
}

Sensors::~Sensors() {
    if (mReadThread.joinable()) {
        char c = 0;
        (void)!write(mStopPipe[1], &c, 1);
        mReadThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(mWriteLock);
        deleteEventFlagLocked();
    }
    mReadWakeLockQueueRun = false;
    if (mWakeLockThread.joinable()) mWakeLockThread.join();
    if (mFd >= 0) close(mFd);
}

HalSensor* Sensors::find(int32_t handle) {
    if (handle < 1 || handle > static_cast<int32_t>(mSensors.size())) return nullptr;
    return &mSensors[handle - 1];
}

// Run each MCU sensor at the fastest rate any of its Android sensors asked for
void Sensors::updateHub(uint32_t ssp) {
    int64_t period = 0;
    for (const auto& s : mSensors) {
        if (s.ssp != ssp || !s.enabled) continue;
        int64_t floor = std::max<int64_t>(s.info.minDelayUs * 1000LL, kMinHubPeriodNs);
        if (s.info.minDelayUs == 0) floor = kMaxHubPeriodNs;  // proximity: reports changes
        int64_t p = std::clamp(s.periodNs, floor, kMaxHubPeriodNs);
        period = period ? std::min(period, p) : p;
    }
    uint32_t ms = static_cast<uint32_t>(period / 1'000'000);
    if (ms == mHubMs[ssp]) return;

    std::string cmd = std::to_string(ssp) + " " + std::to_string(ms);
    if (write(mFd, cmd.c_str(), cmd.size()) < 0) {
        PLOG(ERROR) << "can't set sensor " << ssp << " to " << ms << " ms";
        return;
    }
    mHubMs[ssp] = ms;
}

ScopedAStatus Sensors::activate(int32_t in_sensorHandle, bool in_enabled) {
    std::lock_guard<std::mutex> lock(mSensorLock);
    HalSensor* s = find(in_sensorHandle);
    if (!s) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    if (s->enabled != in_enabled) {
        s->enabled = in_enabled;
        s->haveLast = false;
        updateHub(s->ssp);
    }
    return ScopedAStatus::ok();
}

ScopedAStatus Sensors::batch(int32_t in_sensorHandle, int64_t in_samplingPeriodNs,
                             int64_t /* in_maxReportLatencyNs */) {
    std::lock_guard<std::mutex> lock(mSensorLock);
    HalSensor* s = find(in_sensorHandle);
    if (!s) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    s->periodNs = in_samplingPeriodNs;
    if (s->enabled) updateHub(s->ssp);
    return ScopedAStatus::ok();
}

ScopedAStatus Sensors::flush(int32_t in_sensorHandle) {
    bool wakeup;
    {
        std::lock_guard<std::mutex> lock(mSensorLock);
        HalSensor* s = find(in_sensorHandle);
        if (!s) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        if (!s->enabled)
            return ScopedAStatus::fromServiceSpecificError(
                    static_cast<int32_t>(BnSensors::ERROR_BAD_VALUE));
        wakeup = s->info.flags & static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_WAKE_UP);
    }

    // No batching: nothing is held back, the flush is complete right away
    Event ev;
    ev.sensorHandle = in_sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    ev.payload.set<EventPayload::Tag::meta>(
            EventPayload::MetaData{.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE});
    postEvents({ev}, wakeup);
    return ScopedAStatus::ok();
}

ScopedAStatus Sensors::getSensorsList(std::vector<SensorInfo>* _aidl_return) {
    std::lock_guard<std::mutex> lock(mSensorLock);
    for (const auto& s : mSensors) _aidl_return->push_back(s.info);
    return ScopedAStatus::ok();
}

void Sensors::convert(const SspEvent& ev, std::vector<Event>& out) {
    float mag[3] = {}, gyro[3] = {};
    int8_t magStatus = 0;
    if (ev.sensor == SSP_MAG) {
        for (int i = 0; i < 3; i++) mag[i] = ev.data[i] * kMagScale * mMagAsa[i];
        magStatus = mMagCal.update(mag);
    } else if (ev.sensor == SSP_GYRO) {
        for (int i = 0; i < 3; i++) gyro[i] = ev.data[i] * kGyroScale;
        mGyroCal.update(gyro, ev.timestamp);
    }

    for (auto& s : mSensors) {
        if (s.ssp != ev.sensor || !s.enabled) continue;
        Event e;
        e.sensorHandle = s.info.sensorHandle;
        e.sensorType = s.info.type;
        e.timestamp = ev.timestamp;

        switch (s.info.type) {
            case SensorType::ACCELEROMETER:
                e.payload.set<EventPayload::Tag::vec3>(EventPayload::Vec3{
                        .x = ev.data[0] * kAccelScale,
                        .y = ev.data[1] * kAccelScale,
                        .z = ev.data[2] * kAccelScale,
                        .status = SensorStatus::ACCURACY_HIGH});
                break;
            case SensorType::GYROSCOPE:
                e.payload.set<EventPayload::Tag::vec3>(EventPayload::Vec3{
                        .x = gyro[0] - mGyroCal.bias[0],
                        .y = gyro[1] - mGyroCal.bias[1],
                        .z = gyro[2] - mGyroCal.bias[2],
                        .status = SensorStatus::ACCURACY_HIGH});
                break;
            case SensorType::MAGNETIC_FIELD:
                e.payload.set<EventPayload::Tag::vec3>(EventPayload::Vec3{
                        .x = mag[0] - mMagCal.bias[0],
                        .y = mag[1] - mMagCal.bias[1],
                        .z = mag[2] - mMagCal.bias[2],
                        .status = static_cast<SensorStatus>(magStatus)});
                break;
            case SensorType::MAGNETIC_FIELD_UNCALIBRATED:
                e.payload.set<EventPayload::Tag::uncal>(EventPayload::Uncal{
                        .x = mag[0],
                        .y = mag[1],
                        .z = mag[2],
                        .xBias = mMagCal.bias[0],
                        .yBias = mMagCal.bias[1],
                        .zBias = mMagCal.bias[2]});
                break;
            case SensorType::PRESSURE:
                e.payload.set<EventPayload::Tag::scalar>(ev.data[0] / 100.0f);  // Pa -> hPa
                break;
            case SensorType::LIGHT:
            case SensorType::PROXIMITY: {
                // light: the white channel as lux (like Samsung's/CM's HAL); proximity: near/far
                float v = s.info.type == SensorType::LIGHT ? static_cast<float>(ev.data[3])
                                                           : (ev.data[0] ? 0.0f : kProxFar);
                if (s.haveLast && s.last == v) continue;
                s.haveLast = true;
                s.last = v;
                e.payload.set<EventPayload::Tag::scalar>(v);
                break;
            }
            default:
                continue;
        }
        out.push_back(e);
    }
}

void Sensors::readLoop() {
    SspEvent buf[64];
    struct pollfd fds[2] = {{.fd = mFd, .events = POLLIN}, {.fd = mStopPipe[0], .events = POLLIN}};

    for (;;) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            PLOG(ERROR) << "poll";
            return;
        }
        if (fds[1].revents) return;
        if (!(fds[0].revents & POLLIN)) continue;

        ssize_t n = read(mFd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            PLOG(ERROR) << "read";
            return;
        }

        std::vector<Event> events, wakeEvents;
        {
            std::lock_guard<std::mutex> lock(mSensorLock);
            for (size_t i = 0; i < static_cast<size_t>(n) / sizeof(SspEvent); i++) {
                std::vector<Event>& out = buf[i].sensor == SSP_PROX ? wakeEvents : events;
                convert(buf[i], out);
            }
        }
        if (!events.empty()) postEvents(events, false);
        if (!wakeEvents.empty()) postEvents(wakeEvents, true);
    }
}

void Sensors::postEvents(const std::vector<Event>& events, bool wakeup) {
    std::lock_guard<std::mutex> lock(mWriteLock);
    if (mEventQueue == nullptr || events.empty()) return;
    if (mEventQueue->write(events.data(), events.size())) {
        // Don't take the wake lock if we can't wake the receiver to avoid holding it
        // indefinitely
        if (mEventQueueFlag == nullptr) return;
        mEventQueueFlag->wake(static_cast<uint32_t>(BnSensors::EVENT_QUEUE_FLAG_BITS_READ_AND_PROCESS));
        if (wakeup) updateWakeLock(events.size(), 0);
    }
}

ScopedAStatus Sensors::initialize(
        const common::fmq::MQDescriptor<Event, SynchronizedReadWrite>& in_eventQueueDescriptor,
        const common::fmq::MQDescriptor<int32_t, SynchronizedReadWrite>& in_wakeLockDescriptor,
        const std::shared_ptr<ISensorsCallback>& in_sensorsCallback) {
    ScopedAStatus result = ScopedAStatus::ok();

    // Ensure that all sensors are disabled
    {
        std::lock_guard<std::mutex> lock(mSensorLock);
        for (auto& s : mSensors) s.enabled = false;
        for (uint32_t ssp = 0; ssp < SSP_SENSOR_MAX; ssp++)
            if (mHubMs[ssp]) updateHub(ssp);
    }

    // Stop the Wake Lock thread if it is currently running
    if (mReadWakeLockQueueRun.load()) {
        mReadWakeLockQueueRun = false;
        mWakeLockThread.join();
    }

    mCallback = in_sensorsCallback;

    {
        // Hold the lock to ensure that re-creation of event flag is atomic
        std::lock_guard<std::mutex> lock(mWriteLock);
        mEventQueue = std::make_unique<AidlMessageQueue<Event, SynchronizedReadWrite>>(
                in_eventQueueDescriptor, true /* resetPointers */);
        deleteEventFlagLocked();
        if (EventFlag::createEventFlag(mEventQueue->getEventFlagWord(), &mEventQueueFlag) !=
            ::android::OK) {
            result = ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        mWakeLockQueue = std::make_unique<AidlMessageQueue<int32_t, SynchronizedReadWrite>>(
                in_wakeLockDescriptor, true /* resetPointers */);
        if (!mCallback || !mEventQueue || !mWakeLockQueue || mEventQueueFlag == nullptr) {
            result = ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }

    mReadWakeLockQueueRun = true;
    mWakeLockThread = std::thread([this] { readWakeLockFMQ(); });
    return result;
}

void Sensors::deleteEventFlagLocked() {
    if (mEventQueueFlag != nullptr) {
        if (EventFlag::deleteEventFlag(&mEventQueueFlag) != ::android::OK)
            LOG(INFO) << "Failed to delete event flag";
    }
}

// Read the Wake Lock FMQ and release the wake lock when appropriate
void Sensors::readWakeLockFMQ() {
    while (mReadWakeLockQueueRun.load()) {
        constexpr int64_t kReadTimeoutNs = 500 * 1000 * 1000;  // 500 ms
        int32_t eventsHandled = 0;

        // Time out after a reasonable amount of time to ensure that any held wake lock is able
        // to be released if it is held for too long
        mWakeLockQueue->readBlocking(&eventsHandled, 1 /* count */, 0 /* readNotification */,
                                     static_cast<uint32_t>(WAKE_LOCK_QUEUE_FLAG_BITS_DATA_WRITTEN),
                                     kReadTimeoutNs);
        updateWakeLock(0 /* eventsWritten */, eventsHandled);
    }
}

// Hold a wake lock while there are unhandled WAKE_UP events
void Sensors::updateWakeLock(int32_t eventsWritten, int32_t eventsHandled) {
    std::lock_guard<std::mutex> lock(mWakeLockLock);
    int32_t newVal = mOutstandingWakeUpEvents + eventsWritten - eventsHandled;
    mOutstandingWakeUpEvents = newVal < 0 ? 0 : newVal;

    if (eventsWritten > 0) {
        mAutoReleaseWakeLockTime = ::android::uptimeMillis() +
                                   static_cast<uint32_t>(WAKE_LOCK_TIMEOUT_SECONDS) * 1000;
    }

    if (!mHasWakeLock && mOutstandingWakeUpEvents > 0 &&
        acquire_wake_lock(PARTIAL_WAKE_LOCK, kWakeLockName) == 0) {
        mHasWakeLock = true;
    } else if (mHasWakeLock) {
        if (::android::uptimeMillis() > mAutoReleaseWakeLockTime) mOutstandingWakeUpEvents = 0;
        if (mOutstandingWakeUpEvents == 0 && release_wake_lock(kWakeLockName) == 0)
            mHasWakeLock = false;
    }
}

ScopedAStatus Sensors::configDirectReport(int32_t /* in_sensorHandle */,
                                          int32_t /* in_channelHandle */,
                                          ISensors::RateLevel /* in_rate */,
                                          int32_t* _aidl_return) {
    *_aidl_return = EX_UNSUPPORTED_OPERATION;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::injectSensorData(const Event& /* in_event */) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::registerDirectChannel(const ISensors::SharedMemInfo& /* in_mem */,
                                             int32_t* _aidl_return) {
    *_aidl_return = EX_UNSUPPORTED_OPERATION;
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::setOperationMode(ISensors::OperationMode in_mode) {
    if (in_mode == ISensors::OperationMode::NORMAL) return ScopedAStatus::ok();
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Sensors::unregisterDirectChannel(int32_t /* in_channelHandle */) {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::sensors
