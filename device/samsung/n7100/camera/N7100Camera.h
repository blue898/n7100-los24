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

#include <linux/videodev2.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

// A media link a camera needs; entities are named as in the media graph (a name may leave out
// the " <bus>-<address>" suffix of an I2C sensor)
struct N7100Link {
    std::string source;
    uint16_t sourcePad;
    std::string sink;
    uint16_t sinkPad;
};

// A subdevice pad with a format that doesn't follow the stream size
struct N7100PadFormat {
    std::string entity;
    uint32_t pad;
    uint32_t code;  // media bus code
    uint32_t width;
    uint32_t height;
};

// One camera: the FIMC capture node it is read from and the media pipeline in front of it.
// The pipeline runs in the media device's "sub-dev" mode, so every subdevice on the way has to be
// given its format before the capture node is configured.
struct N7100CameraInfo {
    std::string id;
    std::string videoNode;  // capture node name in sysfs, e.g. "fimc.0.capture"
    uint8_t facing;         // ANDROID_LENS_FACING_*
    int32_t orientation;    // ANDROID_SENSOR_ORIENTATION
    std::string sensor;     // sensor subdevice; output sizes are enumerated on its pad 0
    uint32_t mbusCode;      // media bus code of the pads that follow the stream size
    std::vector<std::pair<std::string, uint32_t>> pads;  // (subdevice, pad) set to the stream size
    std::vector<std::pair<uint32_t, uint32_t>> brokenSizes;  // enumerated, but no frames arrive
    // Fixed output sizes (FIMC scales), for a sensor without a discrete size list
    std::vector<std::pair<uint32_t, uint32_t>> sizes;
    std::vector<N7100Link> links;           // enabled before the capture node is opened
    std::vector<N7100PadFormat> openFormats;  // set before the capture node is opened
    std::vector<N7100PadFormat> fifoFormats;  // set after it is opened, before its S_FMT
    bool autofocus;         // the sensor has AF controls; otherwise fixed focus
    bool exposureBias;      // the sensor's AE takes a bias of -2..2 EV in 1/2 EV steps
    float focalLength;      // mm
    float aperture;         // f-number
    float physicalSize[2];  // sensor size in mm
    std::string flashLed;   // LED class device of the flash, empty without one
};

// The cameras this device has
const std::vector<N7100CameraInfo>& n7100Cameras();

// The flash LED of a camera in torch mode, one per camera for the whole process (the provider
// makes a new ExternalCameraDevice for every getCameraDeviceInterface()). It serves the torch
// requests of cameraserver while the camera is closed and FLASH_MODE_TORCH while it is open, and
// reports its status changes to the provider.
class N7100Torch {
  public:
    enum class Status { NOT_AVAILABLE, AVAILABLE_OFF, AVAILABLE_ON };  // as TorchModeStatus
    using Listener = std::function<void(const std::string& cameraId, Status status)>;

    // The camera's torch, nullptr if it has no flash
    static N7100Torch* get(const N7100CameraInfo& info);
    static void setListener(Listener listener);

    int maxLevel() const { return mMaxLevel; }
    int level();
    // Torch mode for cameraserver: -EBUSY while the camera is open
    int set(bool on, int level);
    // An open camera owns the LED: it goes off and is only lit by setCapture()
    void opened();
    void closed();
    void setCapture(bool on);

  private:
    explicit N7100Torch(const N7100CameraInfo& info);
    void write(int brightness);
    void notify(Status status);

    const std::string mCameraId;
    const std::string mPath;  // brightness of the LED
    const int mMaxLevel;      // torch current steps
    std::mutex mLock;
    bool mOpen = false;
    bool mOn = false;  // torch mode, or FLASH_MODE_TORCH of the open camera
    int mLevel;
};

class N7100Pipeline {
  public:
    explicit N7100Pipeline(const N7100CameraInfo& info);

    const N7100CameraInfo& info() const { return mInfo; }
    // /dev path of the capture node, empty if it doesn't exist
    const std::string& videoPath() const { return mVideoPath; }

    // Output sizes: the fixed list, or the discrete sizes of the sensor
    std::vector<std::pair<uint32_t, uint32_t>> enumSizes() const;
    // Enable the links and set the formats needed before the capture node is opened
    int prepare() const;
    // Set the stream size (and the fixed FIFO formats) on the subdevices of the chain
    int configure(uint32_t width, uint32_t height) const;

    // One autofocus scan; the lens stays where it ends up until the next scan
    void startFocus() const;
    // The area the next scan focuses on, in pixels of the stream (touch AF); none: the centre
    void setFocusArea(const std::optional<v4l2_rect>& area) const;
    // The sensor's continuous autofocus
    void setContinuousFocus(bool on) const;
    // V4L2_AUTO_FOCUS_STATUS_* of the current/last scan
    uint32_t focusStatus() const;
    // AE bias in 1/2 EV steps, -4..4
    void setExposureBias(int halfSteps) const;

  private:
    // /dev path of the video4linux node (video* or v4l-subdev*) with this name
    static std::string findNode(const std::string& name);
    static int setPadFormat(const N7100PadFormat& format);
    int setupLinks() const;
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
