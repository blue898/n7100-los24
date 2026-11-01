/*
 * Galaxy Note II (GT-N7100) cameras on the mainline exynos4-is driver
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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

// One camera: the FIMC capture node it is read from and the media pipeline in front of it.
// The pipeline runs in the media device's "sub-dev" mode, so every subdevice on the way has to be
// given the stream format before the capture node is configured.
struct N7100CameraInfo {
    std::string id;
    std::string videoNode;  // capture node name in sysfs, e.g. "fimc.0.capture"
    uint8_t facing;         // ANDROID_LENS_FACING_*
    int32_t orientation;    // ANDROID_SENSOR_ORIENTATION
    std::string sensor;     // sensor subdevice; output sizes are enumerated on its pad 0
    uint32_t mbusCode;      // media bus code on the whole chain
    std::vector<std::pair<std::string, uint32_t>> pads;  // (subdevice, pad) set to the stream size
    std::vector<std::pair<uint32_t, uint32_t>> brokenSizes;  // enumerated, but no frames arrive
    float focalLength;      // mm
    float aperture;         // f-number
    float physicalSize[2];  // sensor size in mm
};

// The cameras this device has
const std::vector<N7100CameraInfo>& n7100Cameras();

class N7100Pipeline {
  public:
    explicit N7100Pipeline(const N7100CameraInfo& info);

    const N7100CameraInfo& info() const { return mInfo; }
    // /dev path of the capture node, empty if it doesn't exist
    const std::string& videoPath() const { return mVideoPath; }

    // Discrete output sizes of the sensor
    std::vector<std::pair<uint32_t, uint32_t>> enumSizes() const;
    // Set the stream size on every subdevice of the chain
    int configure(uint32_t width, uint32_t height) const;

    // One autofocus scan (the sensor's continuous AF never finds focus); the lens stays where
    // it ends up until the next scan
    void startFocus() const;
    // V4L2_AUTO_FOCUS_STATUS_* of the current/last scan
    uint32_t focusStatus() const;

  private:
    // /dev path of the video4linux node (video* or v4l-subdev*) with this name
    static std::string findNode(const std::string& name);
    int setSensorControl(uint32_t id, int32_t value) const;

    const N7100CameraInfo mInfo;
    std::string mVideoPath;
    std::string mSensorPath;
};

}  // namespace implementation
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
