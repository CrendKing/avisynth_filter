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
    static auto CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 4> &dstSlices, const std::array<int, 4> &dstStrides, int rowSize, int height) -> void;
    static auto CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 4> &srcSlices, const std::array<int, 4> &srcStrides, BYTE *dstBuffer, int rowSize, int height) -> void;

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
    static inline const __m128i _SHUFFLE_MASK_UV_M128_C1 = _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
    static inline const __m128i _SHUFFLE_MASK_UV_M128_C2 = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
    static inline const __m256i _SHUFFLE_MASK_UV_M256_C1 = _mm256_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15, 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
    static inline const __m256i _SHUFFLE_MASK_UV_M256_C2 = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
    static constexpr const int  _PERMUTE_INDEX_UV        = 0b11011000;
    static inline const __m128i _SHUFFLE_MASK_YUVA_M128  = _mm_setr_epi8(0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15);
    static inline const __m256i _SHUFFLE_MASK_YUVA_M256  = _mm256_setr_epi8(0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15, 0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15);
    static inline const __m256i _PERMUTE_INDEX_YUVA      = _mm256_setr_epi8(0, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 6, 0, 0, 0, 3, 0, 0, 0, 7, 0, 0, 0);

    static inline size_t _vectorSize;

    static auto InterleaveY416(std::array<const BYTE *, 4> srcs, int srcStride, BYTE *dst, int dstStride, int rowSize, int height) -> void;

    /*
     * intrinsicType: 1 = SSSE3, 2 = AVX
     * componentSize is the size for each YUV pixel component (1 for 8-bit, 2 for 10 and 16-bit)
     * numDsts is the number of destination buffers (2 or 4)
     */
    template <int intrinsicType, int componentSize, int numDsts>
    static constexpr auto Deinterleave(const BYTE *src, int srcStride, std::array<BYTE *, 4> dsts, int dstStride, int rowSize, int height) -> void {
        /*
         * Place bytes from each plane in sequence by shuffling, then write the sequence of bytes to respective buffer.
         *
         * For AVX2, because its 256-bit shuffle instruction does not operate cross the 128-bit lane,
         * we first prepare the two 128-bit integers just like the SSSE3 version, then permute to correct the order.
         * Much like 0, 2, 1, 3 -> 0, 1, 2, 3.
         */

        // Vector is the type for the memory data each SIMD intrustion works on (__m128i, __m256i, etc.)
        using Vector = std::conditional_t<intrinsicType == 1, __m128i
                     , std::conditional_t<intrinsicType == 2, __m256i
                     , std::array<BYTE, componentSize * 2>>>;
        // Output is the type for the output of the SIMD instructions, half the size of Vector
        using Output = std::array<BYTE, sizeof(Vector) / numDsts>;

        Vector shuffleMask {};
        if constexpr (intrinsicType == 1) {
            if constexpr (componentSize == 1) {
                shuffleMask = _SHUFFLE_MASK_UV_M128_C1;
            } else if constexpr (numDsts == 2) {
                shuffleMask = _SHUFFLE_MASK_UV_M128_C2;
            } else {
                shuffleMask = _SHUFFLE_MASK_YUVA_M128;
            }
        } else if constexpr (intrinsicType == 2) {
            if constexpr (componentSize == 1) {
                shuffleMask = _SHUFFLE_MASK_UV_M256_C1;
            } else if constexpr (numDsts == 2) {
                shuffleMask = _SHUFFLE_MASK_UV_M256_C2;
            } else {
                shuffleMask = _SHUFFLE_MASK_YUVA_M256;
            }
        }

        for (int y = 0; y < height; ++y) {
            const Vector *srcLine = reinterpret_cast<const Vector *>(src);
            std::array<Output *, numDsts> dstsLine;
            for (int p = 0; p < numDsts; ++p) {
                dstsLine[p] = reinterpret_cast<Output *>(dsts[p]);
            }

            for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector)); ++i) {
                const Vector srcVec = *srcLine++;
                Vector outputVec;

                if constexpr (intrinsicType == 1) {
                    outputVec = _mm_shuffle_epi8(srcVec, shuffleMask);
                } else if constexpr (intrinsicType == 2) {
                    const Vector srcShuffle = _mm256_shuffle_epi8(srcVec, shuffleMask);

                    if constexpr (numDsts == 2) {
                        outputVec = _mm256_permute4x64_epi64(srcShuffle, _PERMUTE_INDEX_UV);
                    } else {
                        outputVec = _mm256_permutevar8x32_epi32(srcShuffle, _PERMUTE_INDEX_YUVA);
                    }
                } else {
                    outputVec = srcVec;
                }

                for (int p = 0; p < numDsts; ++p) {
                    *dstsLine[p]++ = *(reinterpret_cast<const Output *>(&outputVec) + p);
                }
            }

            src += srcStride;
            for (int p = 0; p < numDsts; ++p) {
                dsts[p] += dstStride;
            }
        }
    }

    template <int intrinsicType, int componentSize>
    static constexpr auto InterleaveUV(const BYTE *src1, const BYTE *src2, int srcStride, BYTE *dst, int dstStride, int rowSize, int height) -> void {
        using Vector = std::conditional_t<intrinsicType == 1, __m128i
                     , std::conditional_t<intrinsicType == 2, __m256i
                     , void>>;  // using illegal type here to make sure we pass correct template types

        for (int y = 0; y < height; ++y) {
            const Vector *src1Line = reinterpret_cast<const Vector *>(src1);
            const Vector *src2Line = reinterpret_cast<const Vector *>(src2);
            Vector *dstLine = reinterpret_cast<Vector *>(dst);

            for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector) * 2); ++i) {
                const Vector src1Vec = *src1Line++;
                const Vector src2Vec = *src2Line++;

                if constexpr (intrinsicType == 1) {
                    if constexpr (componentSize == 1) {
                        *dstLine++ = _mm_unpacklo_epi8(src1Vec, src2Vec);
                        *dstLine++ = _mm_unpackhi_epi8(src1Vec, src2Vec);
                    } else {
                        *dstLine++ = _mm_unpacklo_epi16(src1Vec, src2Vec);
                        *dstLine++ = _mm_unpackhi_epi16(src1Vec, src2Vec);
                    }
                } else if constexpr (intrinsicType == 2) {
                    const Vector src1Permute = _mm256_permute4x64_epi64(src1Vec, _PERMUTE_INDEX_UV);
                    const Vector src2Permute = _mm256_permute4x64_epi64(src2Vec, _PERMUTE_INDEX_UV);

                    if constexpr (componentSize == 1) {
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

    template <int intrinsicType, int shiftSize, bool isRightShift>
    constexpr static auto BitShiftEach16BitInt(BYTE *bytes, int stride, int rowSize, int height) -> void {
        using Vector = std::conditional_t<intrinsicType == 1, __m128i
                     , std::conditional_t<intrinsicType == 2, __m256i
                     , void>>;

        for (int y = 0; y < height; ++y) {
            Vector *bytesLine = reinterpret_cast<Vector *>(bytes);

            for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector)); ++i) {
                if constexpr (intrinsicType == 1) {
                    if constexpr (isRightShift) {
                        *bytesLine++ = _mm_srli_epi16(*bytesLine, shiftSize);
                    } else {
                        *bytesLine++ = _mm_slli_epi16(*bytesLine, shiftSize);
                    }
                } else if constexpr (intrinsicType == 2) {
                    if constexpr (isRightShift) {
                        *bytesLine++ = _mm256_srli_epi16(*bytesLine, shiftSize);
                    } else {
                        *bytesLine++ = _mm256_slli_epi16(*bytesLine, shiftSize);
                    }
                }
            }

            bytes += stride;
        }
    }
};

}
