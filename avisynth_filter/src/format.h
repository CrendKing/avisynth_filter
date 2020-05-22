#pragma once

#include "pch.h"


class Format {
public:
    struct Definition {
        const CLSID &mediaSubtype;
        int avsType;
        uint8_t bitCount;

        // in case of planar formats, these numbers are for the default plane
        uint8_t componentsPerPixel;
    };

    struct VideoFormat {
        int definition;
        VideoInfo videoInfo;
        BITMAPINFOHEADER bmi;
        VIDEOINFOHEADER *vih;

        auto operator!=(const VideoFormat &other) const -> bool;
    };

    static auto LookupMediaSubtype(const CLSID &mediaSubtype) -> int;
    static auto LookupAvsType(int avsType) -> std::vector<int>;
    static auto GetBitmapInfo(AM_MEDIA_TYPE &mediaType) -> BITMAPINFOHEADER *;
    static auto GetVideoFormat(const AM_MEDIA_TYPE &mediaType) -> VideoFormat;
    static auto CopyFromInput(const VideoFormat &format, const BYTE *srcBuffer, BYTE *dstSlices[], const int dstStrides[], int dstRowSize, int dstHeight, IScriptEnvironment *avsEnv) -> void;
    static auto CopyToOutput(const VideoFormat &format, const BYTE *srcSlices[], const int srcStrides[], BYTE *dstBuffer, int srcRowSize, int srcHeight, IScriptEnvironment *avsEnv) -> void;

    static const std::vector<Definition> DEFINITIONS;

private:
    static auto Deinterleave(const BYTE *src, int srcStride, BYTE *dst1, BYTE *dst2, int dstStride, int rowSize, int height, __m128i mask1, __m128i mask2) -> void;
    static auto Interleave(const BYTE *src1, const BYTE *src2, int srcStride, BYTE *dst, int dstStride, int rowSize, int height, uint8_t bytesPerComponent) -> void;
};
