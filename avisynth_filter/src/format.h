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
        auto GetCodecName() const -> std::string;
    };

    static auto LookupMediaSubtype(const CLSID &mediaSubtype) -> int;
    static auto LookupAvsType(int avsType) -> std::vector<int>;

    template<typename T, typename = std::enable_if_t<std::is_base_of_v<AM_MEDIA_TYPE, std::decay_t<T>>>>
    static auto GetBitmapInfo(T &mediaType) {
        if (SUCCEEDED(CheckVideoInfoType(&mediaType))) {
            return HEADER(mediaType.pbFormat);
        }

        if (SUCCEEDED(CheckVideoInfo2Type(&mediaType))) {
            return &reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType.pbFormat)->bmiHeader;
        }

        return static_cast<BITMAPINFOHEADER *>(nullptr);
    }

    static auto GetVideoFormat(const AM_MEDIA_TYPE &mediaType) -> VideoFormat;
    static auto WriteSample(const VideoFormat &format, PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void;
    static auto CreateFrame(const VideoFormat &format, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> PVideoFrame;
    static auto CopyFromInput(const VideoFormat &format, const BYTE *srcBuffer, BYTE *dstSlices[], const int dstStrides[], int dstRowSize, int dstHeight, IScriptEnvironment *avsEnv) -> void;
    static auto CopyToOutput(const VideoFormat &format, const BYTE *srcSlices[], const int srcStrides[], BYTE *dstBuffer, int srcRowSize, int srcHeight, IScriptEnvironment *avsEnv) -> void;

    static const std::vector<Definition> DEFINITIONS;

private:
    static auto Deinterleave(const BYTE *src, int srcStride, BYTE *dst1, BYTE *dst2, int dstStride, int rowSize, int height, __m128i mask1, __m128i mask2) -> void;
    static auto Interleave(const BYTE *src1, const BYTE *src2, int srcStride, BYTE *dst, int dstStride, int rowSize, int height, uint8_t bytesPerComponent) -> void;
};
