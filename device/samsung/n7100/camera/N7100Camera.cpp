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
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <dirent.h>
#include <string.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <thread>
#include <fcntl.h>
#include <linux/media-bus-format.h>
#include <linux/media.h>
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
    // Front: the S5K6A3 is a raw sensor; FIMC-IS (the Cortex-A5 ISP in the SoC, running the
    // stock firmware) processes its Bayer frames: S5K6A3 -> MIPI CSIS1 -> FIMC-LITE1 -> FIMC-IS
    // ISP -> FIMC1 (FIFO input, YUV 4:4:4 30-bit). The ISP runs the sensor at 1296x732, FIMC
    // scales. It rounds heights up to multiples of 16; 640x360 hangs the stream, so does
    // 256x144.
    constexpr uint32_t kRaw = MEDIA_BUS_FMT_SGRBG10_1X10;
    constexpr uint32_t kIsp = MEDIA_BUS_FMT_YUV10_1X30;
    static const std::vector<N7100CameraInfo> cameras = {
            // Rear: S5C73M3 (8 MP, its own ISP) -> OIF -> MIPI CSIS0 -> FIMC0, YUV 4:2:2 on the
            // bus
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
                    .autofocus = true,
                    .exposureBias = true,
                    .focalLength = 3.7f,
                    .aperture = 2.6f,
                    .physicalSize = {4.54f, 3.42f},
                    // MAX77693 flash LED; torch current 140 mA in 8 steps (device tree)
                    .flashLed = "white:flash",
            },
            {
                    .id = "1",
                    .videoNode = "fimc.1.capture",
                    .facing = ANDROID_LENS_FACING_FRONT,
                    .orientation = 270,
                    .sensor = "S5K6A3",
                    .sizes = {{1280, 720}, {1024, 576}, {768, 432}, {512, 288}},
                    .links = {{"s5p-mipi-csis.1", 1, "FIMC-LITE.1", 0},
                              {"FIMC-LITE.1", 2, "FIMC-IS-ISP", 0},
                              {"FIMC-IS-ISP", 1, "FIMC.1", 1}},
                    .openFormats = {{"S5K6A3", 0, kRaw, 1296, 732},
                                    {"s5p-mipi-csis.1", 0, kRaw, 1296, 732},
                                    {"s5p-mipi-csis.1", 1, kRaw, 1296, 732},
                                    {"FIMC-LITE.1", 0, kRaw, 1296, 732},
                                    {"FIMC-LITE.1", 2, kRaw, 1296, 732},
                                    {"FIMC-IS-ISP", 0, kRaw, 1296, 732},
                                    // less the ISP's 16x12 margin
                                    {"FIMC-IS-ISP", 1, kIsp, 1280, 720}},
                    // The first open of the capture node resets this pad to a format a FIFO
                    // input can't take
                    .fifoFormats = {{"FIMC.1", 1, kIsp, 1280, 720}},
                    .autofocus = false,
                    // Estimates: 1.75 um pixels over 1296x732, the focal length for a ~65 degree
                    // horizontal field of view
                    .focalLength = 1.78f,
                    .aperture = 2.4f,
                    .physicalSize = {2.27f, 1.28f},
            },
    };
    return cameras;
}

N7100Pipeline::N7100Pipeline(const N7100CameraInfo& info)
    : mInfo(info), mVideoPath(findNode(info.videoNode)), mSensorPath(findNode(info.sensor)) {}

// An entity name in the table may leave out the " <bus>-<address>" an I2C sensor gets
static bool entityNameMatches(const std::string& actual, const std::string& wanted) {
    return actual == wanted ||
           (actual.size() > wanted.size() && actual.compare(0, wanted.size(), wanted) == 0 &&
            actual[wanted.size()] == ' ');
}

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
        if (entityNameMatches(base::Trim(node), name)) {
            return std::string("/dev/") + de->d_name;
        }
    }
    ALOGV("%s: no video4linux node named %s", __FUNCTION__, name.c_str());
    return "";
}

std::vector<std::pair<uint32_t, uint32_t>> N7100Pipeline::enumSizes() const {
    if (!mInfo.sizes.empty()) {
        return mInfo.sizes;
    }
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

int N7100Pipeline::setupLinks() const {
    if (mInfo.links.empty()) {
        return 0;
    }
    // The media device whose graph has the entities: all names of its entities by id
    for (int i = 0;; i++) {
        std::string path = "/dev/media" + std::to_string(i);
        unique_fd fd(open(path.c_str(), O_RDWR));
        if (fd < 0) {
            ALOGE("%s: no media device with entity %s", __FUNCTION__,
                  mInfo.links[0].source.c_str());
            return -ENODEV;
        }
        std::vector<std::pair<std::string, uint32_t>> entities;
        media_entity_desc ent{.id = MEDIA_ENT_ID_FLAG_NEXT};
        while (ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &ent) == 0) {
            entities.emplace_back(ent.name, ent.id);
            ent.id |= MEDIA_ENT_ID_FLAG_NEXT;
        }
        auto entityId = [&entities](const std::string& name) -> int64_t {
            for (const auto& [entityName, id] : entities) {
                if (entityNameMatches(entityName, name)) {
                    return id;
                }
            }
            return -1;
        };
        if (entityId(mInfo.links[0].source) < 0) {
            continue;
        }
        for (const auto& link : mInfo.links) {
            int64_t source = entityId(link.source), sink = entityId(link.sink);
            if (source < 0 || sink < 0) {
                ALOGE("%s: no entity %s or %s in %s", __FUNCTION__, link.source.c_str(),
                      link.sink.c_str(), path.c_str());
                return -ENODEV;
            }
            media_link_desc desc{.flags = MEDIA_LNK_FL_ENABLED};
            desc.source.entity = source;
            desc.source.index = link.sourcePad;
            desc.sink.entity = sink;
            desc.sink.index = link.sinkPad;
            if (ioctl(fd, MEDIA_IOC_SETUP_LINK, &desc) < 0) {
                ALOGE("%s: link %s:%u -> %s:%u failed: %s", __FUNCTION__, link.source.c_str(),
                      link.sourcePad, link.sink.c_str(), link.sinkPad, strerror(errno));
                return -errno;
            }
        }
        return 0;
    }
}

int N7100Pipeline::setPadFormat(const N7100PadFormat& format) {
    unique_fd fd(open(findNode(format.entity).c_str(), O_RDWR));
    if (fd < 0) {
        ALOGE("%s: can't open %s: %s", __FUNCTION__, format.entity.c_str(), strerror(errno));
        return -ENODEV;
    }
    v4l2_subdev_format fmt{.which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = format.pad};
    fmt.format.width = format.width;
    fmt.format.height = format.height;
    fmt.format.code = format.code;
    fmt.format.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) < 0) {
        ALOGE("%s: %s:%u S_FMT failed: %s", __FUNCTION__, format.entity.c_str(), format.pad,
              strerror(errno));
        return -errno;
    }
    if (fmt.format.width != format.width || fmt.format.height != format.height ||
        fmt.format.code != format.code) {
        ALOGE("%s: %s:%u wants %ux%u/%#x instead of %ux%u/%#x", __FUNCTION__,
              format.entity.c_str(), format.pad, fmt.format.width, fmt.format.height,
              fmt.format.code, format.width, format.height, format.code);
        return -EINVAL;
    }
    return 0;
}

int N7100Pipeline::prepare() const {
    // The capture node's first open powers the pipeline its enabled links lead to
    int ret = setupLinks();
    for (const auto& format : mInfo.openFormats) {
        if (ret == 0) {
            ret = setPadFormat(format);
        }
    }
    return ret;
}

int N7100Pipeline::configure(uint32_t width, uint32_t height) const {
    for (const auto& format : mInfo.fifoFormats) {
        if (int ret = setPadFormat(format); ret != 0) {
            return ret;
        }
    }
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

    // Continuous autofocus is off until the session asks for it
    if (mInfo.autofocus) {
        setSensorControl(V4L2_CID_FOCUS_AUTO, 0);
    }
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

void N7100Pipeline::setFocusArea(const std::optional<v4l2_rect>& area) const {
    // V4L2_CID_S5C73M3_AF_AREA (kernel patch 0120): a struct v4l2_rect in pixels of the ISP's
    // output frame, which is the stream; an area that covers the frame focuses on the centre
    constexpr uint32_t kCidAfArea = V4L2_CID_USER_BASE + 0x1240;
    v4l2_rect rect = area.value_or(v4l2_rect{0, 0, 0xffff, 0xffff});
    v4l2_ext_control ctrl{.id = kCidAfArea, .size = sizeof(rect)};
    ctrl.ptr = &rect;
    v4l2_ext_controls ctrls{.which = V4L2_CTRL_WHICH_CUR_VAL, .count = 1, .controls = &ctrl};
    unique_fd fd(open(mSensorPath.c_str(), O_RDWR));
    if (fd < 0 || ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        ALOGW("%s: (%d,%d)/%ux%u: %s", __FUNCTION__, rect.left, rect.top, rect.width,
              rect.height, strerror(errno));
    }
}

void N7100Pipeline::setContinuousFocus(bool on) const {
    setSensorControl(V4L2_CID_FOCUS_AUTO, on);
}

uint32_t N7100Pipeline::focusStatus() const {
    unique_fd fd(open(mSensorPath.c_str(), O_RDWR));
    v4l2_control ctrl{.id = V4L2_CID_AUTO_FOCUS_STATUS};
    if (fd < 0 || ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        return V4L2_AUTO_FOCUS_STATUS_FAILED;
    }
    return ctrl.value;
}

void N7100Pipeline::setExposureBias(int halfSteps) const {
    // an integer menu of -2..2 EV, the menu index is the value
    setSensorControl(V4L2_CID_AUTO_EXPOSURE_BIAS, std::clamp(halfSteps, -4, 4) + 4);
}

namespace {
std::mutex sTorchLock;  // sTorches, sTorchListener
std::map<std::string, std::unique_ptr<N7100Torch>> sTorches;  // camera id -> torch
N7100Torch::Listener sTorchListener;

// Torch status changes go out in order on a thread of their own: cameraserver may hold locks its
// callback needs while it waits for open() or setTorchMode()
std::mutex sNotifyLock;
std::condition_variable sNotifyCv;
std::deque<std::function<void()>> sNotifications;
bool sNotifierRunning = false;

void postNotification(std::function<void()> notification) {
    std::lock_guard lock(sNotifyLock);
    sNotifications.push_back(std::move(notification));
    if (!sNotifierRunning) {
        sNotifierRunning = true;
        std::thread([] {
            std::unique_lock lock(sNotifyLock);
            for (;;) {
                sNotifyCv.wait(lock, [] { return !sNotifications.empty(); });
                auto notification = std::move(sNotifications.front());
                sNotifications.pop_front();
                lock.unlock();
                notification();
                lock.lock();
            }
        }).detach();
    }
    sNotifyCv.notify_one();
}

// Torch current steps of the LED (its brightness range in torch mode)
int torchLevels(const std::string& led) {
    std::string s;
    int levels = 0;
    if (!android::base::ReadFileToString("/sys/class/leds/" + led + "/max_brightness", &s) ||
        !android::base::ParseInt(android::base::Trim(s), &levels, 1)) {
        ALOGE("flash %s: no max_brightness", led.c_str());
        return 1;
    }
    return levels;
}
}  // namespace

N7100Torch* N7100Torch::get(const N7100CameraInfo& info) {
    if (info.flashLed.empty()) return nullptr;
    std::lock_guard lock(sTorchLock);
    auto& torch = sTorches[info.id];
    if (!torch) torch.reset(new N7100Torch(info));
    return torch.get();
}

void N7100Torch::setListener(Listener listener) {
    std::lock_guard lock(sTorchLock);
    sTorchListener = std::move(listener);
}

N7100Torch::N7100Torch(const N7100CameraInfo& info)
    : mCameraId(info.id),
      mPath("/sys/class/leds/" + info.flashLed + "/brightness"),
      mMaxLevel(torchLevels(info.flashLed)),
      mLevel(mMaxLevel) {
    write(0);  // a previous provider process may have left it on
}

int N7100Torch::level() {
    std::lock_guard lock(mLock);
    return mLevel;
}

int N7100Torch::set(bool on, int level) {
    {
        std::lock_guard lock(mLock);
        if (mOpen) return -EBUSY;
        // setTorchMode() turns it on at the default level, the full torch current
        mLevel = level > 0 ? std::min(level, mMaxLevel) : mMaxLevel;
        mOn = on;
        write(on ? mLevel : 0);
    }
    notify(on ? Status::AVAILABLE_ON : Status::AVAILABLE_OFF);
    return 0;
}

void N7100Torch::opened() {
    {
        std::lock_guard lock(mLock);
        mOpen = true;
        mOn = false;
        write(0);
    }
    notify(Status::NOT_AVAILABLE);
}

void N7100Torch::closed() {
    {
        std::lock_guard lock(mLock);
        if (!mOpen) return;
        mOpen = false;
        mOn = false;
        write(0);
    }
    notify(Status::AVAILABLE_OFF);
}

void N7100Torch::setCapture(bool on) {
    std::lock_guard lock(mLock);
    if (!mOpen || on == mOn) return;
    mOn = on;
    write(on ? mMaxLevel : 0);
}

void N7100Torch::write(int brightness) {
    if (!android::base::WriteStringToFile(std::to_string(brightness), mPath)) {
        ALOGE("%s: %s = %d: %s", __FUNCTION__, mPath.c_str(), brightness, strerror(errno));
    }
}

void N7100Torch::notify(Status status) {
    Listener listener;
    {
        std::lock_guard lock(sTorchLock);
        listener = sTorchListener;
    }
    if (listener) postNotification([listener, id = mCameraId, status] { listener(id, status); });
}

}  // namespace implementation
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
