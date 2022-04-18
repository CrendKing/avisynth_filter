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
        _deinterleaveUVC1Func = Deinterleave<2, 1, 2>;
        _deinterleaveUVC2Func = Deinterleave<2, 2, 2>;
        _deinterleaveY416Func = Deinterleave<2, 2, 4>;
        _interleaveUVC1Func = InterleaveUV<2, 1>;
        _interleaveUVC2Func = InterleaveUV<2, 2>;
        _rightShiftFunc = BitShiftEach16BitInt<2, 6, true>;
        _leftShiftFunc = BitShiftEach16BitInt<2, 6, false>;
        _vectorSize = sizeof(__m256i);
    } else if (Environment::GetInstance().IsSupportSSSE3()) {
        _deinterleaveUVC1Func = Deinterleave<1, 1, 2>;
        _deinterleaveUVC2Func = Deinterleave<1, 2, 2>;
        _deinterleaveY416Func = Deinterleave<1, 2, 4>;
        _interleaveUVC1Func = InterleaveUV<1, 1>;
        _interleaveUVC2Func = InterleaveUV<1, 2>;
        _rightShiftFunc = BitShiftEach16BitInt<1, 6, true>;
        _leftShiftFunc = BitShiftEach16BitInt<1, 6, false>;
        _vectorSize = sizeof(__m128i);
    } else {
        _deinterleaveUVC1Func = Deinterleave<0, 1, 2>;
        _deinterleaveUVC2Func = Deinterleave<0, 2, 2>;
        _deinterleaveY416Func = Deinterleave<0, 2, 4>;
        _interleaveUVC1Func = InterleaveUV<0, 1>;
        _interleaveUVC2Func = InterleaveUV<0, 2>;
        _rightShiftFunc = BitShiftEach16BitInt<0, 6, true>;
        _leftShiftFunc = BitShiftEach16BitInt<0, 6, false>;
        _vectorSize = 0;
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

}
