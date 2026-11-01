/*
 * Galaxy Note II sensors HAL: the SSP sensor hub (accelerometer, gyroscope, magnetometer,
 * barometer, light and proximity) behind /dev/ssp_sensorhub (drivers/misc/samsung-ssp.c).
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

#pragma once

#include <aidl/android/hardware/common/fmq/SynchronizedReadWrite.h>
#include <aidl/android/hardware/sensors/BnSensors.h>
#include <android-base/thread_annotations.h>
#include <fmq/AidlMessageQueue.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace aidl::android::hardware::sensors {

using aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::android::AidlMessageQueue;
using ::android::hardware::EventFlag;

// One sample from /dev/ssp_sensorhub (struct ssp_event in the kernel driver)
struct SspEvent {
    int64_t timestamp;  // CLOCK_BOOTTIME, ns
    int32_t data[4];
    uint32_t sensor;
    uint32_t reserved;
};

// Sensor numbers of the MCU
enum : uint32_t {
    SSP_ACCEL = 0,
    SSP_GYRO = 1,
    SSP_MAG = 2,
    SSP_PRESSURE = 3,
    SSP_PROX = 5,
    SSP_LIGHT = 6,
    SSP_SENSOR_MAX = 8,
};

struct HalSensor {
    SensorInfo info;
    uint32_t ssp;
    bool enabled = false;
    int64_t periodNs = 200'000'000;
    bool haveLast = false;  // on-change sensors: last reported value
    float last = 0;
};

// Hard-iron offset estimate from the extremes seen on each axis, kept across reboots
class MagCalibration {
  public:
    void load();
    // Updates the estimate with a raw sample (uT), returns the accuracy (SensorStatus)
    int8_t update(const float raw[3]);
    float bias[3] = {0, 0, 0};

  private:
    void save();
    float mMin[3] = {1e9f, 1e9f, 1e9f};
    float mMax[3] = {-1e9f, -1e9f, -1e9f};
    bool mValid = false;
    int64_t mLastSaveNs = 0;
};

// Gyroscope zero-rate offset (about 3 dps on this device), re-estimated whenever it lies still
class GyroCalibration {
  public:
    void update(const float v[3], int64_t timestamp);  // rad/s, uncorrected
    float bias[3] = {0, 0, 0};

  private:
    float mSum[3] = {0, 0, 0};
    float mMin[3] = {0, 0, 0};
    float mMax[3] = {0, 0, 0};
    int mCount = 0;
    int64_t mStart = 0;
};

class Sensors : public BnSensors {
    static constexpr const char* kWakeLockName = "SensorsHAL_WAKEUP";

  public:
    Sensors();
    ~Sensors() override;

    ndk::ScopedAStatus activate(int32_t in_sensorHandle, bool in_enabled) override;
    ndk::ScopedAStatus batch(int32_t in_sensorHandle, int64_t in_samplingPeriodNs,
                             int64_t in_maxReportLatencyNs) override;
    ndk::ScopedAStatus configDirectReport(int32_t in_sensorHandle, int32_t in_channelHandle,
                                          ISensors::RateLevel in_rate,
                                          int32_t* _aidl_return) override;
    ndk::ScopedAStatus flush(int32_t in_sensorHandle) override;
    ndk::ScopedAStatus getSensorsList(std::vector<SensorInfo>* _aidl_return) override;
    ndk::ScopedAStatus initialize(
            const common::fmq::MQDescriptor<Event, SynchronizedReadWrite>& in_eventQueueDescriptor,
            const common::fmq::MQDescriptor<int32_t, SynchronizedReadWrite>& in_wakeLockDescriptor,
            const std::shared_ptr<ISensorsCallback>& in_sensorsCallback) override;
    ndk::ScopedAStatus injectSensorData(const Event& in_event) override;
    ndk::ScopedAStatus registerDirectChannel(const ISensors::SharedMemInfo& in_mem,
                                             int32_t* _aidl_return) override;
    ndk::ScopedAStatus setOperationMode(ISensors::OperationMode in_mode) override;
    ndk::ScopedAStatus unregisterDirectChannel(int32_t in_channelHandle) override;

  private:
    HalSensor* find(int32_t handle) REQUIRES(mSensorLock);
    void addSensor(SensorType type, const char* name, const char* vendor, uint32_t ssp,
                   float maxRange, float resolution, float power, int32_t minDelayUs,
                   int32_t maxDelayUs, uint32_t flags) REQUIRES(mSensorLock);
    void updateHub(uint32_t ssp) REQUIRES(mSensorLock);
    void readLoop();
    void convert(const SspEvent& ev, std::vector<Event>& out) REQUIRES(mSensorLock);
    void postEvents(const std::vector<Event>& events, bool wakeup);
    void deleteEventFlagLocked() REQUIRES(mWriteLock);
    void readWakeLockFMQ();
    void updateWakeLock(int32_t eventsWritten, int32_t eventsHandled);

    int mFd = -1;
    int mStopPipe[2] = {-1, -1};
    std::thread mReadThread;

    std::mutex mSensorLock;
    std::vector<HalSensor> mSensors GUARDED_BY(mSensorLock);  // handle = index + 1
    uint32_t mHubMs[SSP_SENSOR_MAX] GUARDED_BY(mSensorLock) = {};
    MagCalibration mMagCal GUARDED_BY(mSensorLock);
    GyroCalibration mGyroCal GUARDED_BY(mSensorLock);
    float mMagAsa[3] = {1, 1, 1};

    std::unique_ptr<AidlMessageQueue<Event, SynchronizedReadWrite>> mEventQueue;
    std::unique_ptr<AidlMessageQueue<int32_t, SynchronizedReadWrite>> mWakeLockQueue;
    EventFlag* mEventQueueFlag GUARDED_BY(mWriteLock) = nullptr;
    std::shared_ptr<ISensorsCallback> mCallback;
    std::mutex mWriteLock;
    std::mutex mWakeLockLock;
    uint32_t mOutstandingWakeUpEvents = 0;
    std::thread mWakeLockThread;
    std::atomic_bool mReadWakeLockQueueRun = false;
    int64_t mAutoReleaseWakeLockTime = 0;
    bool mHasWakeLock = false;
};

}  // namespace aidl::android::hardware::sensors
