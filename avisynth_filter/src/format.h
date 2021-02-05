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

    static auto Init() -> void;
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

    /*
     * When (de-)interleaving the data from the buffers for U and V planes, if the stride is
     * not a multiple of the vector size, we can't use intrinsics to bulk copy the remainder bytes.
     * Traditionally we copy these bytes in a loop.
     *
     * However, if we allocate the buffer size with some headroom, we can keep using the same
     * logic with intrinsics, simplifying the code. The junk data in the padding will be harmless.
     *
     * Maximum padding is needed when there are minimum remainder bytes, which is 1 byte for each
     * U and V planes (total 2 bytes). Thus padding size is:
     *
     * size of data taken by the intrinsics per interation - 2
     */
    static size_t INPUT_MEDIA_SAMPLE_BUFFER_PADDING;
    static size_t OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING;

private:
    static const __m128i _MASK_SHUFFLE_C8_V128;
    static const __m128i _MASK_SHUFFLE_C16_V128;
    static const int _MASK_PERMUTE_V256;

    static __m256i _MASK_SHUFFLE_C8_V256;
    static __m256i _MASK_SHUFFLE_C16_V256;
    static size_t _vectorSize;

    // PixelComponentSize is the size for each YUV pixel component (8-bit, 10-bit, 16-bit, etc.)
    // Vector is the type for the memory data each SIMD intrustion works on (__m128i, __m256i, etc.)

    template <int PixelComponentSize, typename Vector>
    constexpr static auto Deinterleave(const BYTE *src, int srcStride, BYTE *dst1, BYTE *dst2, int dstStride, int rowSize, int height) -> void {
        Vector shuffleMask;
        if constexpr (std::is_same_v<Vector, __m256i>) {
            if constexpr (PixelComponentSize == 1) {
                shuffleMask = _MASK_SHUFFLE_C8_V256;
            } else {
                shuffleMask = _MASK_SHUFFLE_C16_V256;
            }
        } else if constexpr (std::is_same_v<Vector, __m128i>) {
            if constexpr (PixelComponentSize == 1) {
                shuffleMask = _MASK_SHUFFLE_C8_V128;
            } else {
                shuffleMask = _MASK_SHUFFLE_C16_V128;
            }
        }

        // Output is the type for the output of the SIMD instructions, half the size of Vector
        using Output = std::conditional_t<std::is_same_v<Vector, __m256i>, __m128i
                     , std::conditional_t<std::is_same_v<Vector, __m128i>, __int64
                     , std::conditional_t<std::is_same_v<Vector, __int32>, __int16
                     , __int8
                     >>>;

        const int iterations = DivideRoundUp(rowSize, sizeof(Vector));

        for (int y = 0; y < height; ++y) {
            const Vector *srcLine = reinterpret_cast<const Vector *>(src);
            Output *dst1Line = reinterpret_cast<Output *>(dst1);
            Output *dst2Line = reinterpret_cast<Output *>(dst2);

            for (int i = 0; i < iterations; ++i) {
                const Vector n = *srcLine++;

                if constexpr (std::is_same_v<Vector, __m256i>) {
                    const Vector srcShuffle = _mm256_shuffle_epi8(n, shuffleMask);
                    const Vector srcPermute = _mm256_permute4x64_epi64(srcShuffle, _MASK_PERMUTE_V256);
                    _mm256_storeu2_m128i(dst2Line++, dst1Line++, srcPermute);
                } else if constexpr (std::is_same_v<Vector, __m128i>) {
                    const Vector srcShuffle = _mm_shuffle_epi8(n, shuffleMask);
                    const Vector srcShift = _mm_srli_si128(srcShuffle, 8);
                    _mm_storeu_si64(dst1Line++, srcShuffle);
                    _mm_storeu_si64(dst2Line++, srcShift);
                } else {
                    *dst1Line++ = static_cast<Output>(n);
                    *dst2Line++ = n >> sizeof(Output) * 8;
                }
            }

            src += srcStride;
            dst1 += dstStride;
            dst2 += dstStride;
        }
    }

    template <int PixelComponentSize, typename Vector>
    constexpr static auto Interleave(const BYTE *src1, const BYTE *src2, int srcStride, BYTE *dst, int dstStride, int rowSize, int height) -> void {
        const int iterations = DivideRoundUp(rowSize, sizeof(Vector) * 2);

        for (int y = 0; y < height; ++y) {
            const Vector *src1Line = reinterpret_cast<const Vector *>(src1);
            const Vector *src2Line = reinterpret_cast<const Vector *>(src2);
            Vector *dstLine = reinterpret_cast<Vector *>(dst);

            for (int i = 0; i < iterations; ++i) {
                const Vector src1Data = *src1Line++;
                const Vector src2Data = *src2Line++;

                if constexpr (std::is_same_v<Vector, __m256i>) {
                    const Vector src1Permute = _mm256_permute4x64_epi64(src1Data, _MASK_PERMUTE_V256);
                    const Vector src2Permute = _mm256_permute4x64_epi64(src2Data, _MASK_PERMUTE_V256);

                    if constexpr (PixelComponentSize == 1) {
                        *dstLine++ = _mm256_unpacklo_epi8(src1Permute, src2Permute);
                        *dstLine++ = _mm256_unpackhi_epi8(src1Permute, src2Permute);
                    } else {
                        *dstLine++ = _mm256_unpacklo_epi16(src1Permute, src2Permute);
                        *dstLine++ = _mm256_unpackhi_epi16(src1Permute, src2Permute);
                    }
                } else if constexpr (std::is_same_v<Vector, __m128i>) {
                    if constexpr (PixelComponentSize == 1) {
                        *dstLine++ = _mm_unpacklo_epi8(src1Data, src2Data);
                        *dstLine++ = _mm_unpackhi_epi8(src1Data, src2Data);
                    } else {
                        *dstLine++ = _mm_unpacklo_epi16(src1Data, src2Data);
                        *dstLine++ = _mm_unpackhi_epi16(src1Data, src2Data);
                    }
                }
            }

            src1 += srcStride;
            src2 += srcStride;
            dst += dstStride;
        }
    }
};

}
