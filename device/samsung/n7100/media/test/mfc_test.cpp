/*
 * mfc-test-n7100: decode an H.264 Annex B stream with the MFC v5 (s5p-mfc, V4L2 stateful
 * decoder) and report what comes out; optionally write the frames detiled to NV12.
 * Debugging aid for the Codec2 decoder, not installed by default: m mfc-test-n7100.
 *
 * usage: mfc-test-n7100 <file.h264> [frames] [out.nv12]
 * The stream needs access unit delimiters (x264: aud=1); SPS and PPS go to the decoder first.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "../nv12mt.h"

namespace {

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do r = ioctl(fd, req, arg);
    while (r < 0 && errno == EINTR);
    return r;
}

std::string findDecoder() {
    DIR* d = opendir("/sys/class/video4linux");
    if (!d) return "";
    std::string found;
    while (dirent* de = readdir(d)) {
        char path[256], name[64] = {};
        snprintf(path, sizeof(path), "/sys/class/video4linux/%s/name", de->d_name);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        if (fgets(name, sizeof(name), f) && !strncmp(name, "s5p-mfc-dec", 11))
            found = std::string("/dev/") + de->d_name;
        fclose(f);
    }
    closedir(d);
    return found;
}

// Annex B NAL units: [offset of the start code, end)
struct Nal {
    size_t start, end;
    int type;
};

std::vector<Nal> splitNals(const std::vector<uint8_t>& s) {
    std::vector<Nal> nals;
    size_t i = 0, n = s.size();
    auto isStart = [&](size_t p) { return p + 3 <= n && s[p] == 0 && s[p + 1] == 0 && s[p + 2] == 1; };
    while (i + 3 <= n && !isStart(i)) i++;
    while (i + 3 <= n) {
        size_t hdr = i + 3, j = hdr;
        while (j + 3 <= n && !isStart(j)) j++;
        if (j + 3 > n) j = n;
        size_t end = j;
        if (end < n && end > hdr && s[end - 1] == 0) end--;  // 4-byte start code of the next
        nals.push_back({i > 0 && s[i - 1] == 0 ? i - 1 : i, end, s[hdr] & 0x1f});
        i = j;
    }
    return nals;
}

double now() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

struct Buf {
    void* addr[2] = {};
    size_t len[2] = {};
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.h264> [frames] [out.nv12]\n", argv[0]);
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    int maxFrames = argc > 2 ? atoi(argv[2]) : 1 << 30;
    FILE* out = argc > 3 ? fopen(argv[3], "wb") : nullptr;

    std::vector<uint8_t> stream;
    {
        FILE* f = fopen(argv[1], "rb");
        if (!f) return perror(argv[1]), 1;
        uint8_t b[65536];
        size_t r;
        while ((r = fread(b, 1, sizeof(b), f)) > 0) stream.insert(stream.end(), b, b + r);
        fclose(f);
    }
    // the header (SPS, PPS) and then one chunk per access unit (they start with an AUD)
    std::vector<std::pair<size_t, size_t>> chunks;
    {
        auto nals = splitNals(stream);
        size_t hdrStart = SIZE_MAX, hdrEnd = 0, auStart = SIZE_MAX;
        bool slices = false;
        for (const auto& nal : nals) {
            if (nal.type == 1 || nal.type == 5) slices = true;
            // the first SPS/PPS (x264 repeats them before every IDR)
            if ((nal.type == 7 || nal.type == 8) && !slices) {
                if (hdrStart == SIZE_MAX) hdrStart = nal.start;
                hdrEnd = nal.end;
            }
            if (nal.type == 9) {
                if (auStart != SIZE_MAX) chunks.push_back({auStart, nal.start});
                auStart = nal.start;
            }
        }
        if (auStart != SIZE_MAX) chunks.push_back({auStart, stream.size()});
        if (hdrStart == SIZE_MAX || chunks.empty()) {
            fprintf(stderr, "need SPS/PPS and access unit delimiters\n");
            return 1;
        }
        chunks.insert(chunks.begin(), {hdrStart, hdrEnd});
    }
    printf("%zu bytes, %zu access units\n", stream.size(), chunks.size() - 1);

    std::string dev = findDecoder();
    int fd = open(dev.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) return perror(dev.c_str()), 1;
    printf("decoder %s\n", dev.c_str());

    // OUTPUT: the compressed stream
    v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    fmt.fmt.pix_mp.num_planes = 1;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 1024 * 1024;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt)) return perror("S_FMT output"), 1;
    v4l2_requestbuffers rb = {};
    rb.count = 4;
    rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    rb.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &rb)) return perror("REQBUFS output"), 1;
    std::vector<Buf> in(rb.count);
    for (unsigned i = 0; i < rb.count; i++) {
        v4l2_plane pl = {};
        v4l2_buffer b = {};
        b.type = rb.type;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        b.length = 1;
        b.m.planes = &pl;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b)) return perror("QUERYBUF output"), 1;
        in[i].len[0] = pl.length;
        in[i].addr[0] = mmap(nullptr, pl.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                             pl.m.mem_offset);
    }
    std::vector<bool> inFree(rb.count, true);
    size_t next = 0;

    auto queueChunk = [&](unsigned i) {
        auto [s, e] = chunks[next++];
        size_t len = e - s;
        memcpy(in[i].addr[0], &stream[s], len);
        v4l2_plane pl = {};
        pl.bytesused = len;
        v4l2_buffer b = {};
        b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        b.length = 1;
        b.m.planes = &pl;
        b.timestamp.tv_usec = next;
        if (xioctl(fd, VIDIOC_QBUF, &b)) perror("QBUF output");
        inFree[i] = false;
    };
    queueChunk(0);
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMON, &type)) return perror("STREAMON output"), 1;

    // CAPTURE: the format is known once the header is parsed
    v4l2_format cf = {};
    cf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (int t = 0; t < 100 && xioctl(fd, VIDIOC_G_FMT, &cf); t++) usleep(10000);
    if (!cf.fmt.pix_mp.width) return perror("G_FMT capture"), 1;
    uint32_t w = cf.fmt.pix_mp.width, h = cf.fmt.pix_mp.height;
    printf("capture %ux%u %.4s, %u planes: %u + %u bytes, stride %u\n", w, h,
           (char*)&cf.fmt.pix_mp.pixelformat, cf.fmt.pix_mp.num_planes,
           cf.fmt.pix_mp.plane_fmt[0].sizeimage, cf.fmt.pix_mp.plane_fmt[1].sizeimage,
           cf.fmt.pix_mp.plane_fmt[0].bytesperline);
    v4l2_crop crop = {};
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    uint32_t vw = w, vh = h;
    if (!xioctl(fd, VIDIOC_G_CROP, &crop)) {
        printf("visible %ux%u at %d,%d\n", crop.c.width, crop.c.height, crop.c.left, crop.c.top);
        vw = crop.c.width;
        vh = crop.c.height;
    }
    v4l2_control ctrl = {V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 0};
    xioctl(fd, VIDIOC_G_CTRL, &ctrl);
    v4l2_requestbuffers crb = {};
    crb.count = ctrl.value + 2;
    crb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    crb.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &crb)) return perror("REQBUFS capture"), 1;
    printf("%u capture buffers (min %d)\n", crb.count, ctrl.value);
    std::vector<Buf> cap(crb.count);
    for (unsigned i = 0; i < crb.count; i++) {
        v4l2_plane pl[2] = {};
        v4l2_buffer b = {};
        b.type = crb.type;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        b.length = 2;
        b.m.planes = pl;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b)) return perror("QUERYBUF capture"), 1;
        for (int p = 0; p < 2; p++) {
            cap[i].len[p] = pl[p].length;
            cap[i].addr[p] = mmap(nullptr, pl[p].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                                  pl[p].m.mem_offset);
        }
        if (xioctl(fd, VIDIOC_QBUF, &b)) return perror("QBUF capture"), 1;
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMON, &type)) return perror("STREAMON capture"), 1;

    std::vector<uint8_t> nv12(vw * vh * 3 / 2);
    int frames = 0;
    double t0 = now(), detileTime = 0;
    bool eos = false;
    while (frames < maxFrames) {
        // feed whatever OUTPUT buffers are free
        for (;;) {
            v4l2_plane pl = {};
            v4l2_buffer b = {};
            b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            b.memory = V4L2_MEMORY_MMAP;
            b.length = 1;
            b.m.planes = &pl;
            if (xioctl(fd, VIDIOC_DQBUF, &b)) break;
            inFree[b.index] = true;
        }
        for (unsigned i = 0; i < in.size() && next < chunks.size(); i++)
            if (inFree[i]) queueChunk(i);
        if (next == chunks.size() && !eos) {
            v4l2_decoder_cmd cmd = {};
            cmd.cmd = V4L2_DEC_CMD_STOP;
            xioctl(fd, VIDIOC_DECODER_CMD, &cmd);
            eos = true;
        }

        // POLLOUT: a bitstream buffer is free again (the decoder may want more input before it
        // outputs a frame), POLLIN: a frame
        pollfd p = {fd, POLLIN | POLLOUT, 0};
        if (poll(&p, 1, 2000) <= 0) {
            printf("nothing for 2 s\n");
            break;
        }
        if (!(p.revents & POLLIN)) continue;
        v4l2_plane pl[2] = {};
        v4l2_buffer b = {};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        b.memory = V4L2_MEMORY_MMAP;
        b.length = 2;
        b.m.planes = pl;
        if (xioctl(fd, VIDIOC_DQBUF, &b)) continue;
        bool last = b.flags & V4L2_BUF_FLAG_LAST;
        if (pl[0].bytesused) {
            frames++;
            double d0 = now();
            // s5p-mfc reports no bytesperline: NV12MT lines are 128-byte aligned
            const unsigned stride = (w + 127) & ~127u;
            nv12mt_to_nv12(static_cast<const uint8_t*>(cap[b.index].addr[0]),
                           cf.fmt.pix_mp.plane_fmt[0].sizeimage / stride,
                           static_cast<const uint8_t*>(cap[b.index].addr[1]),
                           cf.fmt.pix_mp.plane_fmt[1].sizeimage / stride, stride, vw, vh,
                           nv12.data(), vw, nv12.data() + vw * vh, vw);
            detileTime += now() - d0;
            if (out) fwrite(nv12.data(), 1, nv12.size(), out);
        }
        if (last) break;
        if (xioctl(fd, VIDIOC_QBUF, &b)) perror("QBUF capture");
    }
    double t = now() - t0;
    printf("%d frames in %.2f s: %.1f fps, detiling %.2f ms/frame\n", frames, t, frames / t,
           frames ? detileTime * 1000 / frames : 0);
    if (out) fclose(out);
    close(fd);
    return frames ? 0 : 1;
}
