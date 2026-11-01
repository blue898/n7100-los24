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

#define LOG_TAG "N7100Camera"
#include <log/log.h>

#include "N7100Camera.h"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <dirent.h>
#include <string.h>
#include <algorithm>
#include <memory>
#include <fcntl.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <system/camera_metadata.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using ::android::base::unique_fd;

const std::vector<N7100CameraInfo>& n7100Cameras() {
    // Rear: S5C73M3 (8 MP, its own ISP) -> OIF -> MIPI CSIS0 -> FIMC0, YUV 4:2:2 on the bus
    static const std::vector<N7100CameraInfo> cameras = {
            {
                    .id = "0",
                    .videoNode = "fimc.0.capture",
                    .facing = ANDROID_LENS_FACING_BACK,
                    .orientation = 90,
                    .sensor = "S5C73M3",
                    .mbusCode = MEDIA_BUS_FMT_VYUY8_2X8,
                    .pads = {{"S5C73M3", 0},
                             {"S5C73M3-OIF", 0},
                             {"S5C73M3-OIF", 2},
                             {"s5p-mipi-csis.0", 0},
                             {"FIMC.0", 0}},
                    .brokenSizes = {{1632, 1224}, {1920, 1440}},
                    .focalLength = 3.7f,
                    .aperture = 2.6f,
                    .physicalSize = {4.54f, 3.42f},
            },
    };
    return cameras;
}

N7100Pipeline::N7100Pipeline(const N7100CameraInfo& info)
    : mInfo(info), mVideoPath(findNode(info.videoNode)), mSensorPath(findNode(info.sensor)) {}

std::string N7100Pipeline::findNode(const std::string& name) {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir("/sys/class/video4linux"), closedir);
    if (!dir) {
        return "";
    }
    while (dirent* de = readdir(dir.get())) {
        std::string node;
        if (de->d_name[0] == '.' ||
            !base::ReadFileToString(std::string("/sys/class/video4linux/") + de->d_name + "/name",
                                    &node)) {
            continue;
        }
        if (base::Trim(node) == name) {
            return std::string("/dev/") + de->d_name;
        }
    }
    ALOGV("%s: no video4linux node named %s", __FUNCTION__, name.c_str());
    return "";
}

std::vector<std::pair<uint32_t, uint32_t>> N7100Pipeline::enumSizes() const {
    std::vector<std::pair<uint32_t, uint32_t>> sizes;
    unique_fd fd(open(mSensorPath.c_str(), O_RDWR));
    if (fd < 0) {
        ALOGE("%s: can't open sensor %s: %s", __FUNCTION__, mInfo.sensor.c_str(), strerror(errno));
        return sizes;
    }
    v4l2_subdev_frame_size_enum fse{
            .pad = 0, .code = mInfo.mbusCode, .which = V4L2_SUBDEV_FORMAT_ACTIVE};
    for (fse.index = 0; ioctl(fd, VIDIOC_SUBDEV_ENUM_FRAME_SIZE, &fse) == 0; fse.index++) {
        std::pair<uint32_t, uint32_t> size{fse.max_width, fse.max_height};
        if (fse.min_width == fse.max_width && fse.min_height == fse.max_height &&
            std::find(mInfo.brokenSizes.begin(), mInfo.brokenSizes.end(), size) ==
                    mInfo.brokenSizes.end()) {
            sizes.push_back(size);
        }
    }
    return sizes;
}

int N7100Pipeline::configure(uint32_t width, uint32_t height) const {
    for (const auto& [entity, pad] : mInfo.pads) {
        unique_fd fd(open(findNode(entity).c_str(), O_RDWR));
        if (fd < 0) {
            ALOGE("%s: can't open %s: %s", __FUNCTION__, entity.c_str(), strerror(errno));
            return -ENODEV;
        }
        v4l2_subdev_format fmt{.which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = pad};
        fmt.format.width = width;
        fmt.format.height = height;
        fmt.format.code = mInfo.mbusCode;
        fmt.format.field = V4L2_FIELD_NONE;
        fmt.format.colorspace = V4L2_COLORSPACE_JPEG;
        if (ioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) < 0) {
            ALOGE("%s: %s:%u S_FMT failed: %s", __FUNCTION__, entity.c_str(), pad, strerror(errno));
            return -errno;
        }
        if (fmt.format.width != width || fmt.format.height != height ||
            fmt.format.code != mInfo.mbusCode) {
            ALOGE("%s: %s:%u wants %ux%u/%#x instead of %ux%u", __FUNCTION__, entity.c_str(), pad,
                  fmt.format.width, fmt.format.height, fmt.format.code, width, height);
            return -EINVAL;
        }
    }

    // No continuous autofocus: in the S5C73M3 firmware it keeps reporting "unfocused" and
    // doesn't move the lens; the session runs single scans instead
    setSensorControl(V4L2_CID_FOCUS_AUTO, 0);
    return 0;
}

int N7100Pipeline::setSensorControl(uint32_t id, int32_t value) const {
    unique_fd fd(open(mSensorPath.c_str(), O_RDWR));
    v4l2_control ctrl{.id = id, .value = value};
    if (fd < 0 || ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        ALOGW("%s: control %#x = %d: %s", __FUNCTION__, id, value, strerror(errno));
        return -errno;
    }
    return 0;
}

void N7100Pipeline::startFocus() const {
    setSensorControl(V4L2_CID_AUTO_FOCUS_START, 1);
}

uint32_t N7100Pipeline::focusStatus() const {
    unique_fd fd(open(mSensorPath.c_str(), O_RDWR));
    v4l2_control ctrl{.id = V4L2_CID_AUTO_FOCUS_STATUS};
    if (fd < 0 || ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        return V4L2_AUTO_FOCUS_STATUS_FAILED;
    }
    return ctrl.value;
}

}  // namespace implementation
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
