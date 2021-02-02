// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "util.h"


namespace AvsFilter {

class Format {
public:
    struct Definition {
        const CLSID &mediaSubtype;
        int avsType;

        // for BITMAPINFOHEADER::biBitCount
        uint8_t bitCount;

        uint8_t componentsPerPixel;

        // ratio between the main plane and the subsampled planes
        int subsampleWidthRatio;
        int subsampleHeightRatio;

        bool areUVPlanesInterleaved;

        int resourceId;
    };

    struct VideoFormat {
        std::wstring name;
        VideoInfo videoInfo;
        int pixelAspectRatio;
        int hdrType;
        int hdrLuminance;
        BITMAPINFOHEADER bmi;

        auto operator!=(const VideoFormat &other) const -> bool;
        auto GetCodecFourCC() const -> DWORD;
    };

    static auto LookupMediaSubtype(const CLSID &mediaSubtype) -> std::optional<std::wstring>;
    static auto LookupAvsType(int avsType) -> std::vector<std::wstring>;

    template<typename T, typename = std::enable_if_t<std::is_base_of_v<AM_MEDIA_TYPE, std::decay_t<T>>>>
    static auto GetBitmapInfo(T &mediaType) -> BITMAPINFOHEADER * {
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
    static auto CopyFromInput(const VideoFormat &format, const BYTE *srcBuffer, BYTE *dstSlices[], const int dstStrides[], int rowSize, int height, IScriptEnvironment *avsEnv) -> void;
    static auto CopyToOutput(const VideoFormat &format, const BYTE *srcSlices[], const int srcStrides[], BYTE *dstBuffer, int rowSize, int height, IScriptEnvironment *avsEnv) -> void;

    static const std::unordered_map<std::wstring, Definition> FORMATS;

private:
    static auto Deinterleave(const BYTE *src, int srcStride, BYTE *dst1, BYTE *dst2, int dstStride, int rowSize, int height, int componentSize) -> void;

    template <typename Component>
    constexpr static auto Interleave(const BYTE *src1, const BYTE *src2, int srcStride, BYTE *dst, int dstStride, int rowSize, int height) -> void {
        const int iterations = DivideRoundUp(rowSize, sizeof(__m128i) * 2);

        for (int y = 0; y < height; ++y) {
            const __m128i *src1_128 = reinterpret_cast<const __m128i *>(src1);
            const __m128i *src2_128 = reinterpret_cast<const __m128i *>(src2);
            __m128i *dst_128 = reinterpret_cast<__m128i *>(dst);

            for (int i = 0; i < iterations; ++i) {
                const __m128i s1 = *src1_128++;
                const __m128i s2 = *src2_128++;

                if constexpr (sizeof(Component) == 1) {
                    *dst_128++ = _mm_unpacklo_epi8(s1, s2);
                    *dst_128++ = _mm_unpackhi_epi8(s1, s2);
                } else {
                    *dst_128++ = _mm_unpacklo_epi16(s1, s2);
                    *dst_128++ = _mm_unpackhi_epi16(s1, s2);
                }
            }

            src1 += srcStride;
            src2 += srcStride;
            dst += dstStride;
        }
    }
};

}
