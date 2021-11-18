// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "util.h"


namespace SynthFilter {

class FrameServerBase;

class Format {
#ifdef AVSF_AVISYNTH
    using FrameServerCore = void *;
    using OutputFrameType = PVideoFrame;
    using VideoInfoType = VideoInfo;
#else
    using FrameServerCore = VSCore *;
    using OutputFrameType = VSFrame *;
    using VideoInfoType = VSVideoInfo;
#endif
    // add const reference or const pointer to the type
    using InputFrameType = std::conditional_t<std::is_pointer_v<OutputFrameType>,
        std::add_pointer_t<std::add_const_t<std::remove_pointer_t<OutputFrameType>>>,
        std::add_lvalue_reference_t<std::add_const_t<OutputFrameType>>
    >;

public:
    struct PixelFormat {
        const WCHAR *name;
        const CLSID &mediaSubtype;
        int frameServerFormatId;

        // for BITMAPINFOHEADER::biBitCount
        uint8_t bitCount;

        // only needed for AviSynth
        uint8_t componentsPerPixel;

        // ratio between the main plane and the subsampled planes
        int subsampleWidthRatio;
        int subsampleHeightRatio;

        bool areUVPlanesInterleaved;

        int resourceId;
    };

    struct VideoFormat {
        struct ColorSpaceInfo {
            std::optional<int> colorRange;
            int primaries = VSColorPrimaries::VSC_PRIMARIES_UNSPECIFIED;
            int matrix = VSMatrixCoefficients::VSC_MATRIX_UNSPECIFIED;
            int transfer = VSTransferCharacteristics::VSC_TRANSFER_UNSPECIFIED;

            auto Update(const DXVA_ExtendedFormat &dxvaExtFormat) -> void;
        };

        const PixelFormat *pixelFormat;
        VideoInfoType videoInfo;
        int64_t pixelAspectRatioNum;
        int64_t pixelAspectRatioDen;
        ColorSpaceInfo colorSpaceInfo;
        int hdrType;
        int hdrLuminance;
        BITMAPINFOHEADER bmi;
        FrameServerCore frameServerCore;

        auto GetCodecFourCC() const -> DWORD { return FOURCCMap(&pixelFormat->mediaSubtype).GetFOURCC(); }
    };

    static auto Initialize() -> void;
    static auto LookupMediaSubtype(const CLSID &mediaSubtype) -> const PixelFormat *;
    static auto LookupFrameServerFormatId(int frameServerFormatId) {
        return PIXEL_FORMATS | std::views::filter([frameServerFormatId](const PixelFormat &pixelFormat) -> bool {
                   return frameServerFormatId == pixelFormat.frameServerFormatId;
               });
    }

    template <typename T, typename = std::enable_if_t<std::is_base_of_v<AM_MEDIA_TYPE, std::decay_t<T>>>>
    static constexpr auto GetBitmapInfo(T &mediaType) -> BITMAPINFOHEADER * {
        if (SUCCEEDED(CheckVideoInfoType(&mediaType))) {
            return HEADER(mediaType.pbFormat);
        }

        if (SUCCEEDED(CheckVideoInfo2Type(&mediaType))) {
            return &reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType.pbFormat)->bmiHeader;
        }

        return nullptr;
    }

    static auto GetVideoFormat(const AM_MEDIA_TYPE &mediaType, const FrameServerBase *frameServerInstance) -> VideoFormat;
    static auto WriteSample(const VideoFormat &videoFormat, InputFrameType srcFrame, BYTE *dstBuffer) -> void;
    static auto CreateFrame(const VideoFormat &videoFormat, const BYTE *srcBuffer) -> OutputFrameType;
    static auto CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 3> &dstSlices, const std::array<int, 3> &dstStrides, int rowSize, int height) -> void;
    static auto CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 3> &srcSlices, const std::array<int, 3> &srcStrides, BYTE *dstBuffer, int rowSize, int height) -> void;

    static const std::vector<PixelFormat> PIXEL_FORMATS;

    /*
     * When (de-)interleaving the data from the buffers for U and V planes, if the stride is
     * not a multiple of the vector size, we can't use intrinsics to bulk copy the remainder bytes.
     * Traditionally we copy these bytes in a loop.
     *
     * However, if we allocate the buffer size with the headroom of one intrinsic size, we can keep using
     * the same logic with intrinsics, simplifying the code. The junk data in the padding will be harmless.
     */
    static inline size_t INPUT_MEDIA_SAMPLE_BUFFER_PADDING;
    static inline size_t OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING;

private:
    static constexpr const int _MASK_PERMUTE_V256 = 0b11011000;
    static inline size_t _vectorSize;

    // intrinsicType: 1 = SSSE3, 2 = AVX
    // PixelComponentSize is the size for each YUV pixel component (8-bit, 10-bit, 16-bit, etc.)

    template <int intrinsicType, int pixelComponentSize>
    static constexpr auto Deinterleave(const BYTE *src, int srcStride, BYTE *dst1, BYTE *dst2, int dstStride, int rowSize, int height) -> void {
        // Vector is the type for the memory data each SIMD intrustion works on (__m128i, __m256i, etc.)
        using Vector = std::conditional_t<intrinsicType == 1, __m128i, std::conditional_t<intrinsicType == 2, __m256i, std::array<BYTE, pixelComponentSize * 2>>>;
        // Output is the type for the output of the SIMD instructions, half the size of Vector
        using Output = std::array<BYTE, sizeof(Vector) / 2>;

        Vector shuffleMask;
        if constexpr (intrinsicType == 1) {
            if constexpr (pixelComponentSize == 1) {
                shuffleMask = _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
            } else {
                shuffleMask = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
            }
        } else if constexpr (intrinsicType == 2) {
            if constexpr (pixelComponentSize == 1) {
                shuffleMask = _mm256_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15, 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
            } else {
                shuffleMask = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
            }
        } else {
            shuffleMask = {};
        }

        for (int y = 0; y < height; ++y) {
            const Vector *srcLine = reinterpret_cast<const Vector *>(src);
            Output *dst1Line = reinterpret_cast<Output *>(dst1);
            Output *dst2Line = reinterpret_cast<Output *>(dst2);

            for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector)); ++i) {
                const Vector srcVec = *srcLine++;
                Vector outputVec;

                if constexpr (intrinsicType == 1) {
                    outputVec = _mm_shuffle_epi8(srcVec, shuffleMask);
                } else if constexpr (intrinsicType == 2) {
                    const Vector srcShuffle = _mm256_shuffle_epi8(srcVec, shuffleMask);
                    outputVec = _mm256_permute4x64_epi64(srcShuffle, _MASK_PERMUTE_V256);
                } else {
                    outputVec = srcVec;
                }

                *dst1Line++ = *reinterpret_cast<Output *>(&outputVec);
                *dst2Line++ = *(reinterpret_cast<Output *>(&outputVec) + 1);
            }

            src += srcStride;
            dst1 += dstStride;
            dst2 += dstStride;
        }
    }

    template <int intrinsicType, int pixelComponentSize>
    static constexpr auto Interleave(const BYTE *src1, const BYTE *src2, int srcStride, BYTE *dst, int dstStride, int rowSize, int height) -> void {
        using Vector = std::conditional_t<intrinsicType == 1,
                                          __m128i,
                                          std::conditional_t<intrinsicType == 2, __m256i, void  // using illegal type here to make sure we pass correct template types
                                                             >>;

        for (int y = 0; y < height; ++y) {
            const Vector *src1Line = reinterpret_cast<const Vector *>(src1);
            const Vector *src2Line = reinterpret_cast<const Vector *>(src2);
            Vector *dstLine = reinterpret_cast<Vector *>(dst);

            for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector) * 2); ++i) {
                const Vector src1Vec = *src1Line++;
                const Vector src2Vec = *src2Line++;

                if constexpr (intrinsicType == 1) {
                    if constexpr (pixelComponentSize == 1) {
                        *dstLine++ = _mm_unpacklo_epi8(src1Vec, src2Vec);
                        *dstLine++ = _mm_unpackhi_epi8(src1Vec, src2Vec);
                    } else {
                        *dstLine++ = _mm_unpacklo_epi16(src1Vec, src2Vec);
                        *dstLine++ = _mm_unpackhi_epi16(src1Vec, src2Vec);
                    }
                } else if constexpr (intrinsicType == 2) {
                    const Vector src1Permute = _mm256_permute4x64_epi64(src1Vec, _MASK_PERMUTE_V256);
                    const Vector src2Permute = _mm256_permute4x64_epi64(src2Vec, _MASK_PERMUTE_V256);

                    if constexpr (pixelComponentSize == 1) {
                        *dstLine++ = _mm256_unpacklo_epi8(src1Permute, src2Permute);
                        *dstLine++ = _mm256_unpackhi_epi8(src1Permute, src2Permute);
                    } else {
                        *dstLine++ = _mm256_unpacklo_epi16(src1Permute, src2Permute);
                        *dstLine++ = _mm256_unpackhi_epi16(src1Permute, src2Permute);
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
