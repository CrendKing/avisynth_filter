// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "environment.h"
#include "format.h"


namespace SynthFilter {

auto Format::VideoFormat::ColorSpaceInfo::Update(const DXVA_ExtendedFormat &dxvaExtFormat) -> void {
    switch (dxvaExtFormat.NominalRange) {
    case DXVA_NominalRange_Normal:
        colorRange = VSColorRange::VSC_RANGE_FULL;
        break;
    case DXVA_NominalRange_Wide:
        colorRange = VSColorRange::VSC_RANGE_LIMITED;
        break;
    }

    switch (dxvaExtFormat.VideoPrimaries) {
    case DXVA_VideoPrimaries_BT709:
        primaries = VSColorPrimaries::VSC_PRIMARIES_BT709;
        break;
    case DXVA_VideoPrimaries_BT470_2_SysM:
        primaries = VSColorPrimaries::VSC_PRIMARIES_BT470_M;
        break;
    case DXVA_VideoPrimaries_BT470_2_SysBG:
        primaries = VSColorPrimaries::VSC_PRIMARIES_BT470_BG;
        break;
    case DXVA_VideoPrimaries_SMPTE170M:
    case DXVA_VideoPrimaries_SMPTE_C:
        primaries = VSColorPrimaries::VSC_PRIMARIES_ST170_M;
        break;
    case DXVA_VideoPrimaries_SMPTE240M:
        primaries = VSColorPrimaries::VSC_PRIMARIES_ST240_M;
        break;
    case DXVA_VideoPrimaries_EBU3213:
        primaries = VSColorPrimaries::VSC_PRIMARIES_EBU3213_E;
        break;
    }

    switch (dxvaExtFormat.VideoTransferMatrix) {
    case DXVA_VideoTransferMatrix_BT709:
        matrix = VSMatrixCoefficients::VSC_MATRIX_BT709;
        break;
    case DXVA_VideoTransferMatrix_BT601:
        matrix = VSMatrixCoefficients::VSC_MATRIX_BT470_BG;
        break;
    case DXVA_VideoTransferMatrix_SMPTE240M:
        matrix = VSMatrixCoefficients::VSC_MATRIX_ST240_M;
        break;
    }

    switch (dxvaExtFormat.VideoTransferFunction) {
    case DXVA_VideoTransFunc_10:
        transfer = VSTransferCharacteristics::VSC_TRANSFER_LINEAR;
        break;
    case DXVA_VideoTransFunc_22:
        transfer = VSTransferCharacteristics::VSC_TRANSFER_BT470_M;
        break;
    case DXVA_VideoTransFunc_22_709:
        transfer = VSTransferCharacteristics::VSC_TRANSFER_BT709;
        break;
    case DXVA_VideoTransFunc_22_240M:
        transfer = VSTransferCharacteristics::VSC_TRANSFER_ST240_M;
        break;
    case DXVA_VideoTransFunc_28:
        transfer = VSTransferCharacteristics::VSC_TRANSFER_BT470_BG;
        break;
    }
}

auto Format::Initialize() -> void {
    if (Environment::GetInstance().IsSupportAVX2()) {
        _UV_SHUFFLE_MASK_M256_C1 = _mm256_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15, 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
        _UV_SHUFFLE_MASK_M256_C2 = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15, 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
        _Y416_SHUFFLE_MASK_M256  = _mm256_setr_epi8(0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15, 0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15);
        _Y416_PERMUTE_INDEX      = _mm256_setr_epi8(0, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 6, 0, 0, 0, 3, 0, 0, 0, 7, 0, 0, 0);

        _deinterleaveUVC1Func = Deinterleave<2, 1, 2, 2>;
        _deinterleaveUVC2Func = Deinterleave<2, 2, 2, 2>;
        _deinterleaveY416Func = Deinterleave<2, 2, 4, 3>;
        _interleaveUVC1Func   = InterleaveUV<2, 1>;
        _interleaveUVC2Func   = InterleaveUV<2, 2>;
        _rightShiftFunc       = BitShiftEach16BitInt<2, 6, true>;
        _leftShiftFunc        = BitShiftEach16BitInt<2, 6, false>;
        _vectorSize           = sizeof(__m256i);
    } else if (Environment::GetInstance().IsSupportSSSE3()) {
        _deinterleaveUVC1Func = Deinterleave<1, 1, 2, 2>;
        _deinterleaveUVC2Func = Deinterleave<1, 2, 2, 2>;
        _deinterleaveY416Func = Deinterleave<1, 2, 4, 3>;
        _interleaveUVC1Func   = InterleaveUV<1, 1>;
        _interleaveUVC2Func   = InterleaveUV<1, 2>;
        _rightShiftFunc       = BitShiftEach16BitInt<1, 6, true>;
        _leftShiftFunc        = BitShiftEach16BitInt<1, 6, false>;
        _vectorSize           = sizeof(__m128i);
    } else {
        _deinterleaveUVC1Func = Deinterleave<0, 1, 2, 2>;
        _deinterleaveUVC2Func = Deinterleave<0, 2, 2, 2>;
        _deinterleaveY416Func = Deinterleave<0, 2, 4, 3>;
        _interleaveUVC1Func   = InterleaveUV<0, 1>;
        _interleaveUVC2Func   = InterleaveUV<0, 2>;
        _rightShiftFunc       = BitShiftEach16BitInt<0, 6, true>;
        _leftShiftFunc        = BitShiftEach16BitInt<0, 6, false>;
        _vectorSize           = 0;
    }

    INPUT_MEDIA_SAMPLE_BUFFER_PADDING = _vectorSize == 0 ? 0 : _vectorSize;
    OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING = (_vectorSize == 0 ? sizeof(__m128i) : _vectorSize) * 2;
}

auto Format::LookupMediaSubtype(const CLSID &mediaSubtype) -> const PixelFormat * {
    for (const PixelFormat &imageFormat : PIXEL_FORMATS) {
        if (mediaSubtype == imageFormat.mediaSubtype) {
            return &imageFormat;
        }
    }

    return nullptr;
}

auto Format::DeinterleaveY410(const BYTE *src, int srcStride, std::array<BYTE *, 3> dsts, const std::array<int, 3> &dstStrides, int rowSize, int height) -> void {
    // process one plane at a time by zeroing all other planes, shuffle it from different pixels together, and fix the position by right shifting

    using Vector = __m128i;
    using Output = uint64_t;

    for (int y = 0; y < height; ++y) {
        const Vector *srcLine = reinterpret_cast<const Vector *>(src);
        std::array<Output *, dsts.size()> dstsLine;
        for (size_t p = 0; p < dsts.size(); ++p) {
            dstsLine[p] = reinterpret_cast<Output *>(dsts[p]);
        }

        for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector)); ++i) {
            const Vector srcVec = *srcLine++;

            const Vector outputVec1 = _mm_shuffle_epi8(_mm_and_si128(srcVec, _Y410_AND_MASK_1), _Y410_SHUFFLE_MASK_1);
            const Vector outputVec2 = _mm_srli_epi32(_mm_shuffle_epi8(_mm_and_si128(srcVec, _Y410_AND_MASK_2), _Y410_SHUFFLE_MASK_2), 2);
            const Vector outputVec3 = _mm_srli_epi32(_mm_shuffle_epi8(_mm_and_si128(srcVec, _Y410_AND_MASK_3), _Y410_SHUFFLE_MASK_3), 4);

            *dstsLine[0]++ = *reinterpret_cast<const Output *>(&outputVec1);
            *dstsLine[1]++ = *reinterpret_cast<const Output *>(&outputVec2);
            *dstsLine[2]++ = *reinterpret_cast<const Output *>(&outputVec3);
        }

        src += srcStride;
        for (size_t p = 0; p < dsts.size(); ++p) {
            dsts[p] += dstStrides[p];
        }
    }
}

auto Format::InterleaveY410(std::array<const BYTE *, 3> srcs, const std::array<int, 3> &srcStrides, BYTE *dst, int dstStride, int rowSize, int height) -> void {
    // expand each 16-bit integer to 32-bit, left shift to right position and OR them all
    // due the expansion, only half the size for each source vector is used, therefore we need to cast

    using Vector = __m128i;
    using Source = uint64_t;

    for (int y = 0; y < height; ++y) {
        std::array<const Source *, srcs.size()> srcsLine;
        for (size_t p = 0; p < srcs.size(); ++p) {
            srcsLine[p] = reinterpret_cast<const Source *>(srcs[p]);
        }
        Vector *dstLine = reinterpret_cast<Vector *>(dst);

        for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector)); ++i) {
            const Vector srcVec1 = _mm_cvtepu16_epi32(*reinterpret_cast<const Vector *>(srcsLine[0]++));
            const Vector srcVec2 = _mm_slli_epi32(_mm_cvtepu16_epi32(*reinterpret_cast<const Vector *>(srcsLine[1]++)), 10);
            const Vector srcVec3 = _mm_slli_epi32(_mm_cvtepu16_epi32(*reinterpret_cast<const Vector *>(srcsLine[2]++)), 20);
            *dstLine++ = _mm_or_si128(_mm_or_si128(srcVec1, srcVec2), srcVec3);
        }

        for (size_t p = 0; p < srcs.size(); ++p) {
            srcs[p] += srcStrides[p];
        }
        dst += dstStride;
    }
}

auto Format::InterleaveY416(std::array<const BYTE *, 3> srcs, const std::array<int, 3> &srcStrides, BYTE *dst, int dstStride, int rowSize, int height) -> void {
    // Extract 32-bit integers from each sources and form 128-bit integer, then shuffle to the correct order

    using Vector = __m128i;
    using Output = uint32_t;

    const Vector shuffleMask = _UV_SHUFFLE_MASK_M128_C2;

    for (int y = 0; y < height; ++y) {
        std::array<const Output *, srcs.size()> srcsLine;
        for (size_t p = 0; p < srcs.size(); ++p) {
            srcsLine[p] = reinterpret_cast<const Output *>(srcs[p]);
        }
        Vector *dstLine = reinterpret_cast<Vector *>(dst);

        for (int i = 0; i < DivideRoundUp(rowSize, sizeof(Vector)); ++i) {
            Vector outputVec = _mm_insert_epi32(_mm_setzero_si128(), *srcsLine[0]++, 0);
            outputVec = _mm_insert_epi32(outputVec, *srcsLine[1]++, 1);
            outputVec = _mm_insert_epi32(outputVec, *srcsLine[2]++, 2);
            *dstLine++ = _mm_shuffle_epi8(outputVec, shuffleMask);
        }

        for (size_t p = 0; p < srcs.size(); ++p) {
            srcs[p] += srcStrides[p];
        }
        dst += dstStride;
    }
}

}
