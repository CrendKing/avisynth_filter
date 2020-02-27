#include "pch.h"
#include "format.h"
#include "g_variables.h"


const std::vector<Format::PixelFormat> Format::FORMATS = {
    { MEDIASUBTYPE_NV12, AVS_CS_YV12, MEDIASUBTYPE_NV12, 1 },
    { MEDIASUBTYPE_YV12, AVS_CS_YV12, MEDIASUBTYPE_NV12, 1 },
    { MEDIASUBTYPE_P010, AVS_CS_YUV420P10, MEDIASUBTYPE_P010, 2 },
    { MEDIASUBTYPE_P016, AVS_CS_YUV420P16, MEDIASUBTYPE_P016, 2 },
};

auto Format::LookupInput(const CLSID &input) -> int {
    for (int i = 0; i < FORMATS.size(); ++i) {
        if (input == FORMATS[i].input) {
            return i;
        }
    };

    return -1;
}

auto Format::LookupOutput(const CLSID &output) -> int {
    for (int i = 0; i < FORMATS.size(); ++i) {
        if (output == FORMATS[i].output) {
            return i;
        }
    };

    return -1;
}

auto Format::Copy(int formatIndex, const BYTE *srcBuffer, int srcUnitStride, BYTE *dstSlices[], const int dstStrides[], int width, int height) -> void {
    const PixelFormat &format = FORMATS[formatIndex];
    const int srcStride = srcUnitStride * format.unitSize;
    const int rowSize = width * format.unitSize;
    const int srcYSize = srcStride * height;

    avs_bit_blt(g_env, dstSlices[0], dstStrides[0], srcBuffer, srcStride, rowSize, height);

    if (IsInterleaved(format.input)) {
        const BYTE *srcUVStart = srcBuffer + srcYSize;

        if (format.unitSize == 1) {
            Deinterleave(reinterpret_cast<const uint8_t *>(srcUVStart),
                         srcUnitStride,
                         reinterpret_cast<uint8_t *>(dstSlices[1]),
                         reinterpret_cast<uint8_t *>(dstSlices[2]),
                         dstStrides[1] / format.unitSize, width / 2, height / 2);
        } else {
            Deinterleave(reinterpret_cast<const uint16_t *>(srcUVStart),
                         srcUnitStride,
                         reinterpret_cast<uint16_t *>(dstSlices[1]),
                         reinterpret_cast<uint16_t *>(dstSlices[2]),
                         dstStrides[1] / format.unitSize, width / 2, height / 2);
        }
    } else {
        const BYTE *srcVStart = srcBuffer + srcYSize;
        const BYTE *srcUStart = srcVStart + srcYSize / 4;

        avs_bit_blt(g_env, dstSlices[1], dstStrides[1], srcUStart, srcStride / 2, rowSize / 2, height / 2);
        avs_bit_blt(g_env, dstSlices[2], dstStrides[1], srcVStart, srcStride / 2, rowSize / 2, height / 2);
    }
}

auto Format::Copy(int formatIndex, const BYTE *srcSlices[], const int srcStrides[], BYTE *dstBuffer, int dstUnitStride, int width, int height) -> void {
    const PixelFormat &format = FORMATS[formatIndex];
    const int dstStride = dstUnitStride * format.unitSize;
    const int rowSize = width * format.unitSize;
    const int dstYSize = dstStride * height;

    avs_bit_blt(g_env, dstBuffer, dstStride, srcSlices[0], srcStrides[0], rowSize, height);

    if (IsInterleaved(format.output)) {
        BYTE *dstUVStart = dstBuffer + dstYSize;

        if (format.unitSize == 1) {
            Interleave(reinterpret_cast<const uint8_t *>(srcSlices[1]),
                       reinterpret_cast<const uint8_t *>(srcSlices[2]),
                       srcStrides[1] / format.unitSize,
                       reinterpret_cast<uint8_t *>(dstUVStart),
                       dstUnitStride, width / 2, height / 2);
        } else {
            Interleave(reinterpret_cast<const uint16_t *>(srcSlices[1]),
                       reinterpret_cast<const uint16_t *>(srcSlices[2]),
                       srcStrides[1] / format.unitSize,
                       reinterpret_cast<uint16_t *>(dstUVStart),
                       dstUnitStride, width / 2, height / 2);
        }
    } else {
        BYTE *dstVStart = dstBuffer + dstYSize;
        BYTE *dstUStart = dstVStart + dstYSize / 4;

        avs_bit_blt(g_env, dstUStart, dstStride / 2, srcSlices[1], srcStrides[1], rowSize / 2, height / 2);
        avs_bit_blt(g_env, dstVStart, dstStride / 2, srcSlices[2], srcStrides[1], rowSize / 2, height / 2);
    }
}

auto Format::IsInterleaved(const CLSID &type) -> bool {
    return type != MEDIASUBTYPE_YV12;
}
