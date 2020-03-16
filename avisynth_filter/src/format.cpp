#include "pch.h"
#include "format.h"
#include "constants.h"


const std::vector<Format::PixelFormat> Format::FORMATS = {
    { MEDIASUBTYPE_NV12, AVS_CS_YV12, MEDIASUBTYPE_NV12, 12, 1, 1 },
    { MEDIASUBTYPE_I420, AVS_CS_YV12, MEDIASUBTYPE_I420, 12, 1, 1 },
    { MEDIASUBTYPE_IYUV, AVS_CS_YV12, MEDIASUBTYPE_IYUV, 12, 1, 1 },
    { MEDIASUBTYPE_YV12, AVS_CS_YV12, MEDIASUBTYPE_YV12, 12, 1, 1 },
    { MEDIASUBTYPE_P010, AVS_CS_YUV420P10, MEDIASUBTYPE_P010, 24, 2, 1 },
    { MEDIASUBTYPE_P016, AVS_CS_YUV420P16, MEDIASUBTYPE_P016, 24, 2, 1 },

    // packed formats such as YUY2 are twice as wide as unpacked formats per pixel
    { MEDIASUBTYPE_YUY2, AVS_CS_YUY2, MEDIASUBTYPE_YUY2, 16, 1, 2 },
    { MEDIASUBTYPE_UYVY, AVS_CS_YUY2, MEDIASUBTYPE_UYVY, 16, 1, 2 },
    { MEDIASUBTYPE_RGB24, AVS_CS_BGR24, MEDIASUBTYPE_RGB24, 24, 1, 3 },
    { MEDIASUBTYPE_RGB32, AVS_CS_BGR32, MEDIASUBTYPE_RGB32, 24, 1, 4 },
};

auto Format::LookupInput(const CLSID &input) -> int {
    for (int i = 0; i < FORMATS.size(); ++i) {
        if (input == FORMATS[i].input) {
            return i;
        }
    }

    return -1;
}

auto Format::LookupOutput(const CLSID &output) -> int {
    for (int i = 0; i < FORMATS.size(); ++i) {
        if (output == FORMATS[i].output) {
            return i;
        }
    }

    return -1;
}

auto Format::CopyFromInput(int formatIndex, const BYTE *srcBuffer, int srcUnitStride, BYTE *dstSlices[], const int dstStrides[], int width, int height, AVS_ScriptEnvironment *avsEnv) -> void {
    const PixelFormat &format = FORMATS[formatIndex];

    const int absHeight = abs(height);
    const int srcStride = srcUnitStride * format.yPixelSize * format.unitSizePerPixel;
    const int rowSize = width * format.yPixelSize * format.unitSizePerPixel;
    const int srcDefaultPlaneSize = srcStride * absHeight;

    const BYTE *srcDefaultPlane;
    int srcDefaultPlaneStride;
    if ((format.avs & AVS_CS_BGR) != 0 && height < 0) {
        // positive height for RGB format is bottom-up DIB, negative is top-down
        // AviSynth is always bottom-up
        srcDefaultPlane = srcBuffer + srcDefaultPlaneSize - srcStride;
        srcDefaultPlaneStride = -srcStride;
    } else {
        srcDefaultPlane = srcBuffer;
        srcDefaultPlaneStride = srcStride;
    }

    avs_bit_blt(avsEnv, dstSlices[0], dstStrides[0], srcDefaultPlane, srcDefaultPlaneStride, rowSize, absHeight);

    if ((format.avs & AVS_CS_INTERLEAVED) == 0) {
        // 4:2:0 unpacked formats

        if (format.input == MEDIASUBTYPE_IYUV || format.input == MEDIASUBTYPE_I420 || format.input == MEDIASUBTYPE_YV12) {
            // these formats' U and V planes are not interleaved. use BitBlt to efficiently copy

            const BYTE *srcPlane1 = srcBuffer + srcDefaultPlaneSize;
            const BYTE *srcPlane2 = srcPlane1 + srcDefaultPlaneSize / 4;

            const BYTE *srcU;
            const BYTE *srcV;
            if (format.input == MEDIASUBTYPE_YV12) {
                // YV12 has V plane first

                srcU = srcPlane2;
                srcV = srcPlane1;
            } else {
                srcU = srcPlane1;
                srcV = srcPlane2;
            }

            avs_bit_blt(avsEnv, dstSlices[1], dstStrides[1], srcU, srcStride / 2, rowSize / 2, absHeight / 2);
            avs_bit_blt(avsEnv, dstSlices[2], dstStrides[1], srcV, srcStride / 2, rowSize / 2, absHeight / 2);
        } else {
            // interleaved U and V planes. copy byte by byte
            // consider using intrinsics for better performance

            const BYTE *srcUVStart = srcBuffer + srcDefaultPlaneSize;

            if (format.yPixelSize == 1) {
                Deinterleave(reinterpret_cast<const uint8_t *>(srcUVStart),
                             srcUnitStride,
                             reinterpret_cast<uint8_t *>(dstSlices[1]),
                             reinterpret_cast<uint8_t *>(dstSlices[2]),
                             dstStrides[1] / format.yPixelSize, width / 2, absHeight / 2);
            } else {
                Deinterleave(reinterpret_cast<const uint16_t *>(srcUVStart),
                             srcUnitStride,
                             reinterpret_cast<uint16_t *>(dstSlices[1]),
                             reinterpret_cast<uint16_t *>(dstSlices[2]),
                             dstStrides[1] / format.yPixelSize, width / 2, absHeight / 2);
            }
        }
    }
}

auto Format::CopyToOutput(int formatIndex, const BYTE *srcSlices[], const int srcStrides[], BYTE *dstBuffer, int dstUnitStride, int width, int height, AVS_ScriptEnvironment *avsEnv) -> void {
    const PixelFormat &format = FORMATS[formatIndex];

    const int absHeight = abs(height);
    const int dstStride = dstUnitStride * format.yPixelSize * format.unitSizePerPixel;
    const int rowSize = width * format.yPixelSize * format.unitSizePerPixel;
    const int dstDefaultPlaneSize = dstStride * absHeight;

    BYTE *dstDefaultPlane;
    int dstDefaultPlaneStride;
    if ((format.avs & AVS_CS_BGR) != 0 && height < 0) {
        dstDefaultPlane = dstBuffer + dstDefaultPlaneSize - dstStride;
        dstDefaultPlaneStride = -dstStride;
    } else {
        dstDefaultPlane = dstBuffer;
        dstDefaultPlaneStride = dstStride;
    }

    avs_bit_blt(avsEnv, dstDefaultPlane, dstDefaultPlaneStride, srcSlices[0], srcStrides[0], rowSize, absHeight);

    if ((format.avs & AVS_CS_INTERLEAVED) == 0) {
        if (format.output == MEDIASUBTYPE_IYUV || format.output == MEDIASUBTYPE_I420 || format.output == MEDIASUBTYPE_YV12) {
            BYTE *dstPlane1 = dstBuffer + dstDefaultPlaneSize;
            BYTE *dstPlane2 = dstPlane1 + dstDefaultPlaneSize / 4;

            BYTE *dstU;
            BYTE *dstV;
            if (format.input == MEDIASUBTYPE_YV12) {
                dstU = dstPlane2;
                dstV = dstPlane1;
            } else {
                dstU = dstPlane1;
                dstV = dstPlane2;
            }

            avs_bit_blt(avsEnv, dstU, dstStride / 2, srcSlices[1], srcStrides[1], rowSize / 2, absHeight / 2);
            avs_bit_blt(avsEnv, dstV, dstStride / 2, srcSlices[2], srcStrides[1], rowSize / 2, absHeight / 2);
        } else {
            BYTE *dstUVStart = dstBuffer + dstDefaultPlaneSize;

            if (format.yPixelSize == 1) {
                Interleave(reinterpret_cast<const uint8_t *>(srcSlices[1]),
                           reinterpret_cast<const uint8_t *>(srcSlices[2]),
                           srcStrides[1] / format.yPixelSize,
                           reinterpret_cast<uint8_t *>(dstUVStart),
                           dstUnitStride, width / 2, absHeight / 2);
            } else {
                Interleave(reinterpret_cast<const uint16_t *>(srcSlices[1]),
                           reinterpret_cast<const uint16_t *>(srcSlices[2]),
                           srcStrides[1] / format.yPixelSize,
                           reinterpret_cast<uint16_t *>(dstUVStart),
                           dstUnitStride, width / 2, absHeight / 2);
            }
        }
    }
}
