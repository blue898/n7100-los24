/*
 * V4L2_PIX_FMT_NV12MT (what the Exynos 4 MFC writes) to linear NV12.
 *
 * Both planes are made of 64x32 byte tiles of 2048 bytes. A pair of tile rows is stored as 2x2
 * groups of tiles in Z order, the groups alternating between a Z and a horizontally mirrored Z
 * (pixfmt-yuv-planar.rst). The line stride is a multiple of 128 bytes; an odd last row of
 * tiles is stored linearly. Tile index as in GStreamer's gst_video_tile_get_index
 * (GST_VIDEO_TILE_MODE_ZFLIPZ_2X2).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline size_t nv12mt_tile_index(unsigned x, unsigned y, unsigned xTiles, unsigned yTiles) {
    size_t offset = (y & ~1u) * xTiles + x;
    if (y & 1) {
        offset += (x & ~3u) + 2;
    } else if ((yTiles & 1) == 0 || y != yTiles - 1) {
        offset += (x + 2) & ~3u;
    }
    return offset;
}

// One tiled plane of srcLines lines (a multiple of 32) into a linear plane: width x height
static inline void nv12mt_detile_plane(const uint8_t* src, unsigned srcStride, unsigned srcLines,
                                       unsigned width, unsigned height, uint8_t* dst,
                                       unsigned dstStride) {
    const unsigned xTiles = srcStride / 64, yTiles = srcLines / 32;
    for (unsigned ty = 0; ty * 32 < height; ty++) {
        const unsigned rows = height - ty * 32 < 32 ? height - ty * 32 : 32;
        for (unsigned tx = 0; tx * 64 < width; tx++) {
            const uint8_t* tile = src + nv12mt_tile_index(tx, ty, xTiles, yTiles) * 2048;
            const unsigned cols = width - tx * 64 < 64 ? width - tx * 64 : 64;
            uint8_t* d = dst + (size_t)ty * 32 * dstStride + tx * 64;
            for (unsigned r = 0; r < rows; r++) memcpy(d + (size_t)r * dstStride, tile + r * 64, cols);
        }
    }
}

// The visible width x height of a frame: luma, then chroma (interleaved UV, height / 2 lines).
// srcYLines/srcUVLines: lines allocated per plane (plane size / stride).
static inline void nv12mt_to_nv12(const uint8_t* srcY, unsigned srcYLines, const uint8_t* srcUV,
                                  unsigned srcUVLines, unsigned srcStride, unsigned width,
                                  unsigned height, uint8_t* dstY, unsigned dstStrideY,
                                  uint8_t* dstUV, unsigned dstStrideUV) {
    nv12mt_detile_plane(srcY, srcStride, srcYLines, width, height, dstY, dstStrideY);
    nv12mt_detile_plane(srcUV, srcStride, srcUVLines, width, height / 2, dstUV, dstStrideUV);
}
