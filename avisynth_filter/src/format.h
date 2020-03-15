#pragma once

#include "pch.h"


class Format {
public:
    struct PixelFormat {
        const CLSID &input;
        int avs;
        const CLSID &output;
        uint8_t bitsPerPixel;
        uint8_t yPixelSize;  // in bytes
        uint8_t unitSizePerPixel;
    };

    static auto LookupInput(const CLSID &input) -> int;
    static auto LookupOutput(const CLSID &output) -> int;
    static auto CopyFromInput(int formatIndex, const BYTE *srcBuffer, int srcUnitStride, BYTE *dstSlices[], const int dstStrides[], int rowSize, int height, AVS_ScriptEnvironment *avsEnv) -> void;
    static auto CopyToOutput(int formatIndex, const BYTE *srcSlices[], const int srcStrides[], BYTE *dstBuffer, int dstUnitStride, int rowSize, int height, AVS_ScriptEnvironment *avsEnv) -> void;

    static const std::vector<PixelFormat> FORMATS;

private:
    template <typename T>
    static auto Deinterleave(const T *src, int srcStride, T *dst1, T *dst2, int dstStride, int width, int height) -> void {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                dst1[x] = src[x * 2 + 0];
                dst2[x] = src[x * 2 + 1];
            }

            src += srcStride;
            dst1 += dstStride;
            dst2 += dstStride;
        }
    }

    template <typename T>
    static auto Interleave(const T *src1, const T *src2, int srcStride, T *dst, int dstStride, int width, int height) -> void {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                dst[x * 2 + 0] = src1[x];
                dst[x * 2 + 1] = src2[x];
            }

            src1 += srcStride;
            src2 += srcStride;
            dst += dstStride;
        }
    }
};
