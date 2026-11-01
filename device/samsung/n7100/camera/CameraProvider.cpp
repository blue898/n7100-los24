/*
 * Camera provider for the Galaxy Note II: a fixed list of cameras (N7100Camera.cpp) served by
 * the device code of AOSP's external camera HAL
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

#define LOG_TAG "N7100CamPrvdr"
#include <log/log.h>

#include <ExternalCameraDevice.h>
#include <convert.h>
#include <N7100Camera.h>
#include <aidl/android/hardware/camera/common/CameraDeviceStatus.h>
#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <map>
#include <mutex>
#include <thread>

#include <unistd.h>

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using ::aidl::android::hardware::camera::common::CameraDeviceStatus;
using ::aidl::android::hardware::camera::common::Status;
using ::aidl::android::hardware::camera::common::VendorTagSection;
using ::aidl::android::hardware::camera::device::ICameraDevice;
using ::aidl::android::hardware::camera::provider::BnCameraProvider;
using ::aidl::android::hardware::camera::provider::CameraIdAndStreamCombination;
using ::aidl::android::hardware::camera::provider::ConcurrentCameraIdCombination;
using ::aidl::android::hardware::camera::provider::ICameraProviderCallback;
using ::android::hardware::camera::device::implementation::ExternalCameraDevice;
using ::android::hardware::camera::device::implementation::fromStatus;
using ::android::hardware::camera::device::implementation::N7100CameraInfo;
using ::android::hardware::camera::device::implementation::N7100Pipeline;
using ::android::hardware::camera::device::implementation::n7100Cameras;
using ::android::hardware::camera::external::common::ExternalCameraConfig;

class CameraProvider : public BnCameraProvider {
  public:
    CameraProvider() : mCfg(ExternalCameraConfig::loadFromCfg(kConfigPath)) {
        if (scan()) return;
        // The capture nodes appear once the camera modules have probed and the media graph is
        // complete, which at boot can be well after this service starts: keep looking, and
        // announce each camera as it shows up
        std::thread([this] {
            for (int tries = 1; tries < kDiscoveryTries && !scan(); tries++)
                usleep(kDiscoveryIntervalUs);
            std::lock_guard lock(mLock);
            for (const auto& info : n7100Cameras()) {
                if (!mCameras.contains(deviceName(info)))
                    ALOGW("camera %s: no capture node %s", info.id.c_str(),
                          info.videoNode.c_str());
            }
        }).detach();
    }

    ndk::ScopedAStatus setCallback(const std::shared_ptr<ICameraProviderCallback>& cb) override {
        if (cb == nullptr) return fromStatus(Status::ILLEGAL_ARGUMENT);
        std::lock_guard lock(mLock);
        mCallback = cb;
        return fromStatus(Status::OK);
    }

    ndk::ScopedAStatus getVendorTags(std::vector<VendorTagSection>* tags) override {
        *tags = {};
        return fromStatus(Status::OK);
    }

    ndk::ScopedAStatus getCameraIdList(std::vector<std::string>* names) override {
        names->clear();
        std::lock_guard lock(mLock);
        for (const auto& [name, info] : mCameras) {
            names->push_back(name);
        }
        return fromStatus(Status::OK);
    }

    ndk::ScopedAStatus getCameraDeviceInterface(const std::string& name,
                                                std::shared_ptr<ICameraDevice>* device) override {
        *device = nullptr;
        const N7100CameraInfo* info;
        {
            std::lock_guard lock(mLock);
            auto it = mCameras.find(name);
            if (it == mCameras.end()) {
                return fromStatus(Status::ILLEGAL_ARGUMENT);
            }
            info = it->second;
        }
        auto impl = ndk::SharedRefBase::make<ExternalCameraDevice>(*info, mCfg);
        if (impl == nullptr || impl->isInitFailed()) {
            ALOGE("%s: camera %s init failed", __FUNCTION__, name.c_str());
            return fromStatus(Status::INTERNAL_ERROR);
        }
        *device = impl;
        return fromStatus(Status::OK);
    }

    ndk::ScopedAStatus notifyDeviceStateChange(int64_t) override {
        return fromStatus(Status::OK);
    }

    ndk::ScopedAStatus getConcurrentCameraIds(
            std::vector<ConcurrentCameraIdCombination>* ids) override {
        *ids = {};
        return fromStatus(Status::OK);
    }

    ndk::ScopedAStatus isConcurrentStreamCombinationSupported(
            const std::vector<CameraIdAndStreamCombination>&, bool* supported) override {
        *supported = false;
        return fromStatus(Status::OK);
    }

  private:
    static constexpr char kConfigPath[] = "/vendor/etc/camera/n7100_camera_config.xml";
    static constexpr int kDiscoveryTries = 120;  // 60 s
    static constexpr useconds_t kDiscoveryIntervalUs = 500000;

    static std::string deviceName(const N7100CameraInfo& info) {
        return "device@" + ExternalCameraDevice::kDeviceVersion + "/internal/" + info.id;
    }

    // Looks for the cameras not found yet and announces new ones; true once all are there.
    // A camera counts once its device initialises: at boot the capture node is there before
    // the sensor has probed, and cameraserver drops a camera whose first open fails.
    bool scan() {
        for (const auto& info : n7100Cameras()) {
            const std::string name = deviceName(info);
            {
                std::lock_guard lock(mLock);
                if (mCameras.contains(name)) continue;
            }
            if (N7100Pipeline(info).videoPath().empty() ||
                ndk::SharedRefBase::make<ExternalCameraDevice>(info, mCfg)->isInitFailed())
                continue;
            std::shared_ptr<ICameraProviderCallback> cb;
            {
                std::lock_guard lock(mLock);
                mCameras.emplace(name, &info);
                cb = mCallback;
            }
            ALOGI("camera %s: %s", info.id.c_str(), info.videoNode.c_str());
            if (cb) cb->cameraDeviceStatusChange(name, CameraDeviceStatus::PRESENT);
        }
        std::lock_guard lock(mLock);
        return mCameras.size() == n7100Cameras().size();
    }

    const ExternalCameraConfig mCfg;
    std::mutex mLock;
    std::map<std::string, const N7100CameraInfo*> mCameras;  // device name -> camera
    std::shared_ptr<ICameraProviderCallback> mCallback;
};

}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android

int main() {
    using ::android::hardware::camera::provider::implementation::CameraProvider;

    ABinderProcess_setThreadPoolMaxThreadCount(6);

    auto provider = ndk::SharedRefBase::make<CameraProvider>();
    const std::string name = std::string(CameraProvider::descriptor) + "/internal/0";
    binder_exception_t ret = AServiceManager_addService(provider->asBinder().get(), name.c_str());
    LOG_ALWAYS_FATAL_IF(ret != EX_NONE, "Error registering camera provider %s: %d", name.c_str(),
                        ret);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
