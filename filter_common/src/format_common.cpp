// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "format.h"
#include "environment.h"


namespace SynthFilter {

auto Format::VideoFormat::ColorSpaceInfo::Update(const DXVA_ExtendedFormat &dxvaExtFormat) -> void {
    switch (dxvaExtFormat.NominalRange) {
    case DXVA_NominalRange_Normal:
        colorRange = 0;
        break;
    case DXVA_NominalRange_Wide:
        colorRange = 1;
        break;
    }

    switch (dxvaExtFormat.VideoPrimaries) {
    case DXVA_VideoPrimaries_BT709:
        primaries = 1;
        break;
    case DXVA_VideoPrimaries_BT470_2_SysM:
        primaries = 4;
        break;
    case DXVA_VideoPrimaries_BT470_2_SysBG:
        primaries = 5;
        break;
    case DXVA_VideoPrimaries_SMPTE170M:
    case DXVA_VideoPrimaries_SMPTE_C:
        primaries = 6;
        break;
    case DXVA_VideoPrimaries_SMPTE240M:
        primaries = 7;
        break;
    case DXVA_VideoPrimaries_EBU3213:
        primaries = 22;
        break;
    }

    switch (dxvaExtFormat.VideoTransferMatrix) {
    case DXVA_VideoTransferMatrix_BT709:
        matrix = 1;
        break;
    case DXVA_VideoTransferMatrix_BT601:
        matrix = 5;
        break;
    case DXVA_VideoTransferMatrix_SMPTE240M:
        matrix = 7;
        break;
    }

    switch (dxvaExtFormat.VideoTransferFunction) {
    case DXVA_VideoTransFunc_10:
        transfer = 8;
        break;
    case DXVA_VideoTransFunc_22:
        transfer = 4;
        break;
    case DXVA_VideoTransFunc_22_709:
        transfer = 1;
        break;
    case DXVA_VideoTransFunc_22_240M:
        transfer = 7;
        break;
    case DXVA_VideoTransFunc_28:
        transfer = 5;
        break;
    }
}

auto Format::Initialize() -> void {
    if (Environment::GetInstance().IsSupportAVXx()) {
        _vectorSize = sizeof(__m256i);
    } else if (Environment::GetInstance().IsSupportSSSE3()) {
        _vectorSize = sizeof(__m128i);
    } else {
        _vectorSize = 0;
    }

    INPUT_MEDIA_SAMPLE_BUFFER_PADDING = _vectorSize == 0 ? 0 : _vectorSize - 2;
    OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING = (_vectorSize == 0 ? sizeof(__m128i) : _vectorSize) * 2 - 2;
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
