// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "environment.h"
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
    enum class PlanesLayout {
        ALL_PLANES_INTERLEAVED,
        MAIN_SEPARATE_SEC_INTERLEAVED,
        ALL_PLANES_SEPARATE,
    };

    struct PixelFormat {
        const WCHAR *name;
        const CLSID &mediaSubtype;
        int frameServerFormatId;

        // for BITMAPINFOHEADER::biBitCount
        uint8_t bitCount;

        // for the DirectShow format
        uint8_t componentsPerPixel;

        // ratio between the main plane and the subsampled planes
        int subsampleWidthRatio;
        int subsampleHeightRatio;

        PlanesLayout srcPlanesLayout;

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
        int64_t pixelAspectRatioNum = 1;
        int64_t pixelAspectRatioDen = 1;
        ColorSpaceInfo colorSpaceInfo;
        int hdrType = 0;
        int hdrLuminance = 0;
        BITMAPINFOHEADER bmi;
        FrameServerCore frameServerCore;

        /*
         * bit 1 is set if the format requires bit shifting
         * bit 2 is set if the protection of the destination sample buffer has been queried
         * bit 3 is set if the protection of the destination sample buffer has PAGE_WRITECOMBINE
         * when all bits are set, CopyToOutput() utilize an intermediate destination buffer
         */
        int outputBufferTemporalFlags = 0;

        auto GetCodecFourCC() const -> DWORD;
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

    static auto GetStrideAlignedMediaSampleSize(const AM_MEDIA_TYPE &mediaType, int strideAlignment) -> long;
    static auto GetVideoFormat(const AM_MEDIA_TYPE &mediaType, const FrameServerBase *frameServerInstance) -> VideoFormat;
    static auto WriteSample(const VideoFormat &videoFormat, InputFrameType srcFrame, BYTE *dstBuffer) -> void;
    static auto CreateFrame(const VideoFormat &videoFormat, const BYTE *srcBuffer) -> OutputFrameType;
    static auto CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 3> &dstSlices, const std::array<int, 3> &dstStrides, int frameWidth, int height) -> void;
    static auto CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 3> &srcSlices, const std::array<int, 3> &srcStrides, BYTE *dstBuffer, int frameWidth, int height) -> void;

    static const std::vector<PixelFormat> PIXEL_FORMATS;

    /*
     * When (de-)interleaving the data from the buffers for U and V planes, if the stride is
     * not a multiple of the vector size, we can't use intrinsics to bulk copy the remainder bytes.
     * Traditionally we copy these bytes in a loop.
     *
     * However, if we allocate the buffer size with the headroom of one intrinsic size, we can keep using
     * the same logic with intrinsics, simplifying the code. The junk data in the padding will be harmless.
     */
    static inline int INPUT_MEDIA_SAMPLE_STRIDE_ALIGNMENT;
    static inline int OUTPUT_MEDIA_SAMPLE_STRIDE_ALIGNMENT;

private:
    static inline const __m128i _UV_SHUFFLE_MASK_M128_C1  = _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
    static inline const __m128i _UV_SHUFFLE_MASK_M128_C2  = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
    static inline       __m256i _UV_SHUFFLE_MASK_M256_C1;
    static inline       __m256i _UV_SHUFFLE_MASK_M256_C2;
    static inline const __m128i _Y410_AND_MASK_1          = _mm_setr_epi32(1023, 1023, 1023, 1023);  // 10 bits of 1s at the least significant side
    static inline const __m128i _Y410_AND_MASK_2          = _mm_setr_epi32(1047552, 1047552, 1047552, 1047552);  // 10 bits of 1s in the middle
    static inline const __m128i _Y410_AND_MASK_3          = _mm_setr_epi32(1072693248, 1072693248, 1072693248, 1072693248);  // 10 bits of 1s at the most significant side
    static inline const __m128i _Y410_SHUFFLE_MASK_1      = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 0, 0, 0, 0, 0, 0, 0, 0);
    static inline const __m128i _Y410_SHUFFLE_MASK_2      = _mm_setr_epi8(1, 2, 5, 6, 9, 10, 13, 14, 0, 0, 0, 0, 0, 0, 0, 0);
    static inline const __m128i _Y410_SHUFFLE_MASK_3      = _mm_setr_epi8(2, 3, 6, 7, 10, 11, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0);
    static inline const __m128i _Y416_SHUFFLE_MASK_M128   = _mm_setr_epi8(0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15);
    static inline       __m256i _Y416_SHUFFLE_MASK_M256;
    static inline const __m128i _RGB_SHUFFLE_MASK_M128_C1 = _mm_setr_epi8(0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);
    static inline       __m256i _RGB_SHUFFLE_MASK_M256_C1;
    static constexpr const int  _UV_PERMUTE_INDEX         = 0b11011000;
    static inline       __m256i _FOUR_PERMUTE_INDEX;

    /*
     * intrinsicType: 1 = SSE4, 2 = AVX2. Anything else: non-SIMD
     * componentSize is the size per pixel component (1 for 8-bit, 2 for 10 and 16-bit)
     * srcNumComponents is the number of components per pixel for the source
     * dstNumComponents is the number of components per pixel for the destination
     * srcNumComponents should always >= dstNumComponents. They differ in case we want to discard certain components (e.g. the alpha plane of Y410/Y416)
     * colorFamily: 1 = YUV, 2 = RGB
     */
    template <int intrinsicType, int componentSize, int srcNumComponents, int dstNumComponents, int colorFamily>
    static constexpr auto Deinterleave(const BYTE *src, int srcStride, std::array<BYTE *, 3> dsts, const std::array<int, 3> &dstStrides, int rowSize, int height) -> void {
        /*
         * Place bytes from each plane in sequence by shuffling, then write the sequence of bytes to respective buffer.
         *
         * For AVX2, because its 256-bit shuffle instruction does not operate cross the 128-bit lane,
         * we first prepare the two 128-bit integers just like the SSSE3 version, then permute to correct the order.
         * Much like 0, 2, 1, 3 -> 0, 1, 2, 3.
         */

        Environment::GetInstance().Log(L"Deinterleave() start");

        // Input is the type for the input data each SIMD intrustion works on (__m128i, __m256i, etc.)
        using Input = std::conditional_t<intrinsicType == 1, __m128i
                    , std::conditional_t<intrinsicType == 2, __m256i
                    , std::array<BYTE, componentSize * srcNumComponents>>>;
        // Output is the type for the output of the SIMD instructions, half the size of Input
        using Output = std::array<BYTE, sizeof(Input) / srcNumComponents>;

        Input shuffleMask;
        (void) shuffleMask;

        if constexpr (intrinsicType == 1) {
            if constexpr (componentSize == 1) {
                if constexpr (colorFamily == 1) {
                    shuffleMask = _UV_SHUFFLE_MASK_M128_C1;
                } else if constexpr (colorFamily == 2) {
                    shuffleMask = _RGB_SHUFFLE_MASK_M128_C1;
                }
            } else if constexpr (srcNumComponents == 2) {
                shuffleMask = _UV_SHUFFLE_MASK_M128_C2;
            } else if constexpr (srcNumComponents == 4) {
                shuffleMask = _Y416_SHUFFLE_MASK_M128;
            }
        } else if constexpr (intrinsicType == 2) {
            if constexpr (componentSize == 1) {
                if constexpr (colorFamily == 1) {
                    shuffleMask = _UV_SHUFFLE_MASK_M256_C1;
                } else if constexpr (colorFamily == 2) {
                    shuffleMask = _RGB_SHUFFLE_MASK_M256_C1;
                }
            } else if constexpr (srcNumComponents == 2) {
                shuffleMask = _UV_SHUFFLE_MASK_M256_C2;
            } else if constexpr (srcNumComponents == 4) {
                shuffleMask = _Y416_SHUFFLE_MASK_M256;
            }
        }

        const int cycles = DivideRoundUp(rowSize, sizeof(Input));

        for (int y = 0; y < height; ++y) {
            const Input *srcLine = reinterpret_cast<const Input *>(src);
            std::array<Output *, dstNumComponents> dstsLine;
            for (int p = 0; p < dstNumComponents; ++p) {
                dstsLine[p] = reinterpret_cast<Output *>(dsts[p]);
            }

            for (int i = 0; i < cycles; ++i) {
                const Input srcVec = *srcLine++;
                Input dataVec;

                if constexpr (intrinsicType == 1) {
                    dataVec = _mm_shuffle_epi8(srcVec, shuffleMask);
                } else if constexpr (intrinsicType == 2) {
                    const Input srcShuffle = _mm256_shuffle_epi8(srcVec, shuffleMask);

                    if constexpr (srcNumComponents == 2) {
                        dataVec = _mm256_permute4x64_epi64(srcShuffle, _UV_PERMUTE_INDEX);
                    } else if constexpr (srcNumComponents == 4) {
                        dataVec = _mm256_permutevar8x32_epi32(srcShuffle, _FOUR_PERMUTE_INDEX);
                    }
                } else {
                    dataVec = srcVec;
                }

                for (int p = 0; p < dstNumComponents; ++p) {
                    *dstsLine[p]++ = *(reinterpret_cast<const Output *>(&dataVec) + p);
                }
            }

            src += srcStride;
            for (int p = 0; p < dstNumComponents; ++p) {
                dsts[p] += dstStrides[p];
            }
        }

        Environment::GetInstance().Log(L"Deinterleave() end");
    }

    template <int intrinsicType, int componentSize>
    static constexpr auto InterleaveUV(const BYTE *src1, const BYTE *src2, int srcStride1, int srcStride2, BYTE *dst, int dstStride, int rowSize, int height) -> void {
        Environment::GetInstance().Log(L"InterleaveUV() start");

        using Vector = std::conditional_t<intrinsicType == 1, __m128i
                     , std::conditional_t<intrinsicType == 2, __m256i
                     , std::array<BYTE, componentSize>>>;

        const int cycles = DivideRoundUp(rowSize, sizeof(Vector) * 2);

        for (int y = 0; y < height; ++y) {
            const Vector *src1Line = reinterpret_cast<const Vector *>(src1);
            const Vector *src2Line = reinterpret_cast<const Vector *>(src2);
            Vector *dstLine = reinterpret_cast<Vector *>(dst);

            for (int i = 0; i < cycles; ++i) {
                const Vector src1Vec = *src1Line++;
                const Vector src2Vec = *src2Line++;

                if constexpr (intrinsicType == 1) {
                    if constexpr (componentSize == 1) {
                        *dstLine++ = _mm_unpacklo_epi8(src1Vec, src2Vec);
                        *dstLine++ = _mm_unpackhi_epi8(src1Vec, src2Vec);
                    } else if constexpr (componentSize == 2) {
                        *dstLine++ = _mm_unpacklo_epi16(src1Vec, src2Vec);
                        *dstLine++ = _mm_unpackhi_epi16(src1Vec, src2Vec);
                    }
                } else if constexpr (intrinsicType == 2) {
                    const Vector src1Permute = _mm256_permute4x64_epi64(src1Vec, _UV_PERMUTE_INDEX);
                    const Vector src2Permute = _mm256_permute4x64_epi64(src2Vec, _UV_PERMUTE_INDEX);

                    if constexpr (componentSize == 1) {
                        *dstLine++ = _mm256_unpacklo_epi8(src1Permute, src2Permute);
                        *dstLine++ = _mm256_unpackhi_epi8(src1Permute, src2Permute);
                    } else if constexpr (componentSize == 2) {
                        *dstLine++ = _mm256_unpacklo_epi16(src1Permute, src2Permute);
                        *dstLine++ = _mm256_unpackhi_epi16(src1Permute, src2Permute);
                    }
                } else {
                    *dstLine++ = src1Vec;
                    *dstLine++ = src2Vec;
                }
            }

            src1 += srcStride1;
            src2 += srcStride2;
            dst += dstStride;
        }

        Environment::GetInstance().Log(L"InterleaveUV() end");
    }

    template <int colorFamily>
    constexpr static auto InterleaveThree(std::array<const BYTE *, 3> srcs, const std::array<int, 3> &srcStrides, BYTE *dst, int dstStride, int rowSize, int height) -> void {
        // Extract 32-bit integers from each sources and form 128-bit integer, then shuffle to the correct order

        Environment::GetInstance().Log(L"InterleaveThree() start");

        using Input = uint32_t;
        using Output = __m128i;

        Output shuffleMask;
        if constexpr (colorFamily == 1) {
            shuffleMask = _UV_SHUFFLE_MASK_M128_C2;
        } else if constexpr (colorFamily == 2) {
            shuffleMask = _RGB_SHUFFLE_MASK_M128_C1;
        }
        const Output initial = _mm_set1_epi8(-1);

        const int cycles = DivideRoundUp(rowSize, sizeof(Output));

        for (int y = 0; y < height; ++y) {
            std::array<const Input *, srcs.size()> srcsLine;
            for (size_t p = 0; p < srcs.size(); ++p) {
                srcsLine[p] = reinterpret_cast<const Input *>(srcs[p]);
            }
            Output *dstLine = reinterpret_cast<Output *>(dst);

            for (int i = 0; i < cycles; ++i) {
                Output vec = _mm_insert_epi32(initial, *srcsLine[0]++, 0);
                vec = _mm_insert_epi32(vec, *srcsLine[1]++, 1);
                vec = _mm_insert_epi32(vec, *srcsLine[2]++, 2);
                *dstLine++ = _mm_shuffle_epi8(vec, shuffleMask);
            }

            for (size_t p = 0; p < srcs.size(); ++p) {
                srcs[p] += srcStrides[p];
            }
            dst += dstStride;
        }

        Environment::GetInstance().Log(L"InterleaveThree() end");
    }

    template <int intrinsicType, int shiftSize, bool isRightShift>
    constexpr static auto BitShiftEach16BitInt(BYTE *src, BYTE *dst, int stride, int rowSize, int height) -> void {
        Environment::GetInstance().Log(L"BitShiftEach16BitInt(%d) start", isRightShift);

        using Vector = std::conditional_t<intrinsicType == 1, __m128i
                     , std::conditional_t<intrinsicType == 2, __m256i
                     , uint16_t>>;

        const int cycles = DivideRoundUp(rowSize, sizeof(Vector));

        for (int y = 0; y < height; ++y) {
            Vector *srcLine = reinterpret_cast<Vector *>(src);
            Vector *dstLine = reinterpret_cast<Vector *>(dst);

            for (int i = 0; i < cycles; ++i) {
                if constexpr (intrinsicType == 1) {
                    if constexpr (isRightShift) {
                        *dstLine++ = _mm_srli_epi16(*srcLine++, shiftSize);
                    } else {
                        *dstLine++ = _mm_slli_epi16(*srcLine++, shiftSize);
                    }
                } else if constexpr (intrinsicType == 2) {
                    if constexpr (isRightShift) {
                        *dstLine++ = _mm256_srli_epi16(*srcLine++, shiftSize);
                    } else {
                        *dstLine++ = _mm256_slli_epi16(*srcLine++, shiftSize);
                    }
                } else {
                    if constexpr (isRightShift) {
                        *dstLine++ = *srcLine++ >> shiftSize;
                    } else {
                        *dstLine++ = *srcLine++ << shiftSize;
                    }
                }
            }

            src += stride;
            dst += stride;
        }

        Environment::GetInstance().Log(L"BitShiftEach16BitInt(%d) end", isRightShift);
    }

    static auto DeinterleaveY410(const BYTE *src, int srcStride, std::array<BYTE *, 3> dsts, const std::array<int, 3> &dstStrides, int rowSize, int height) -> void;
    static auto InterleaveY410(std::array<const BYTE *, 3> srcs, const std::array<int, 3> &srcStrides, BYTE *dst, int dstStride, int rowSize, int height) -> void;

    static inline decltype(Deinterleave<0, 1, 2, 2, 1>) *_deinterleaveUVC1Func;
    static inline decltype(Deinterleave<0, 2, 2, 2, 1>) *_deinterleaveUVC2Func;
    static inline decltype(Deinterleave<0, 2, 4, 3, 1>) *_deinterleaveY416Func;
    static inline decltype(Deinterleave<0, 1, 4, 3, 2>) *_deinterleaveRGBC1Func;
    static inline decltype(InterleaveUV<0, 1>) *_interleaveUVC1Func;
    static inline decltype(InterleaveUV<0, 2>) *_interleaveUVC2Func;
    static inline decltype(InterleaveThree<1>) *_interleaveY416Func;
    static inline decltype(InterleaveThree<2>) *_interleaveRGBC1Func;
    static inline decltype(BitShiftEach16BitInt<0, 6, true>) *_rightShiftFunc;
    static inline decltype(BitShiftEach16BitInt<0, 6, false>) *_leftShiftFunc;

    static inline int _vectorSize;
};

}
