// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "format.h"

#include "constants.h"
#include "frameserver.h"


namespace SynthFilter {

// for each group of formats with the same format ID, they should appear with the most preferred -> least preferred order
const std::vector<Format::PixelFormat> Format::PIXEL_FORMATS {
    // 4:2:0
    { .name = L"NV12",  .mediaSubtype = MEDIASUBTYPE_NV12,  .frameServerFormatId = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 2,  .srcPlanesLayout = PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED, .resourceId = IDC_INPUT_FORMAT_NV12 },
    { .name = L"YV12",  .mediaSubtype = MEDIASUBTYPE_YV12,  .frameServerFormatId = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 2,  .srcPlanesLayout = PlanesLayout::ALL_PLANES_SEPARATE,           .resourceId = IDC_INPUT_FORMAT_YV12 },
    { .name = L"I420",  .mediaSubtype = MEDIASUBTYPE_I420,  .frameServerFormatId = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 2,  .srcPlanesLayout = PlanesLayout::ALL_PLANES_SEPARATE,           .resourceId = IDC_INPUT_FORMAT_I420 },
    { .name = L"IYUV",  .mediaSubtype = MEDIASUBTYPE_IYUV,  .frameServerFormatId = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 2,  .srcPlanesLayout = PlanesLayout::ALL_PLANES_SEPARATE,           .resourceId = IDC_INPUT_FORMAT_IYUV },

    // P010 from DirectShow has the least significant 6 bits zero-padded, while AviSynth expects the most significant bits zeroed
    // Therefore, there will be bit shifting whenever P010 is used
    { .name = L"P010",  .mediaSubtype = MEDIASUBTYPE_P010,  .frameServerFormatId = VideoInfo::CS_YUV420P10, .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 2,  .srcPlanesLayout = PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED, .resourceId = IDC_INPUT_FORMAT_P010 },
    { .name = L"P016",  .mediaSubtype = MEDIASUBTYPE_P016,  .frameServerFormatId = VideoInfo::CS_YUV420P16, .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 2,  .srcPlanesLayout = PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED, .resourceId = IDC_INPUT_FORMAT_P016 },

    // 4:2:2
    // YUY2 interleaves Y and UV planes together, thus twice as wide as unpacked formats per pixel
    { .name = L"YUY2",  .mediaSubtype = MEDIASUBTYPE_YUY2,  .frameServerFormatId = VideoInfo::CS_YUY2,      .bitCount = 16, .componentsPerPixel = 2, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 1,  .srcPlanesLayout = PlanesLayout::ALL_PLANES_INTERLEAVED,        .resourceId = IDC_INPUT_FORMAT_YUY2 },
    { .name = L"P210",  .mediaSubtype = MEDIASUBTYPE_P210,  .frameServerFormatId = VideoInfo::CS_YUV422P10, .bitCount = 32, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 1,  .srcPlanesLayout = PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED, .resourceId = IDC_INPUT_FORMAT_P210 },
    { .name = L"P216",  .mediaSubtype = MEDIASUBTYPE_P216,  .frameServerFormatId = VideoInfo::CS_YUV422P16, .bitCount = 32, .componentsPerPixel = 1, .subsampleWidthRatio = 2,  .subsampleHeightRatio = 1,  .srcPlanesLayout = PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED, .resourceId = IDC_INPUT_FORMAT_P216 },

    // 4:4:4
    { .name = L"YV24",  .mediaSubtype = MEDIASUBTYPE_YV24,  .frameServerFormatId = VideoInfo::CS_YV24,      .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 1,  .subsampleHeightRatio = 1,  .srcPlanesLayout = PlanesLayout::ALL_PLANES_SEPARATE,           .resourceId = IDC_INPUT_FORMAT_YV24 },
    // Y41x from DirectShow contains alpha plane, which is used during video playback, therefore we ignore it and feed frame server YUV444
    { .name = L"Y410",  .mediaSubtype = MEDIASUBTYPE_Y410,  .frameServerFormatId = VideoInfo::CS_YUV444P10, .bitCount = 32, .componentsPerPixel = 4, .subsampleWidthRatio = 1,  .subsampleHeightRatio = 1,  .srcPlanesLayout = PlanesLayout::ALL_PLANES_INTERLEAVED,        .resourceId = IDC_INPUT_FORMAT_Y410 },
    { .name = L"Y416",  .mediaSubtype = MEDIASUBTYPE_Y416,  .frameServerFormatId = VideoInfo::CS_YUV444P16, .bitCount = 64, .componentsPerPixel = 4, .subsampleWidthRatio = 1,  .subsampleHeightRatio = 1,  .srcPlanesLayout = PlanesLayout::ALL_PLANES_INTERLEAVED,        .resourceId = IDC_INPUT_FORMAT_Y416 },

    // RGB
    { .name = L"RGB24", .mediaSubtype = MEDIASUBTYPE_RGB24, .frameServerFormatId = VideoInfo::CS_BGR24,     .bitCount = 24, .componentsPerPixel = 3, .subsampleWidthRatio = -1, .subsampleHeightRatio = -1, .srcPlanesLayout = PlanesLayout::ALL_PLANES_INTERLEAVED,        .resourceId = IDC_INPUT_FORMAT_RGB24 },
    { .name = L"RGB32", .mediaSubtype = MEDIASUBTYPE_RGB32, .frameServerFormatId = VideoInfo::CS_BGR32,     .bitCount = 32, .componentsPerPixel = 4, .subsampleWidthRatio = -1, .subsampleHeightRatio = -1, .srcPlanesLayout = PlanesLayout::ALL_PLANES_INTERLEAVED,        .resourceId = IDC_INPUT_FORMAT_RGB32 },
    // RGB48 from LAV Filters outputs R-G-B pixel order while AviSynth+ expects B-G-R
};

auto Format::GetVideoFormat(const AM_MEDIA_TYPE &mediaType, const FrameServerBase *frameServerInstance) -> VideoFormat {
    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    const REFERENCE_TIME frameDuration = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;

    VideoFormat ret {
        .pixelFormat = LookupMediaSubtype(mediaType.subtype),
        .videoInfo = {
            .width = vih->rcSource.right - vih->rcSource.left,
            .height = vih->rcSource.bottom - vih->rcSource.top,
            .fps_numerator = UNITS,
            .fps_denominator = static_cast<unsigned int>(frameDuration),
            .num_frames = NUM_FRAMES_FOR_INFINITE_STREAM,
            .pixel_type = ret.pixelFormat->frameServerFormatId,
        },
        .bmi = *GetBitmapInfo(mediaType),
        .outputBufferTemporalFlags = ret.pixelFormat->srcPlanesLayout == PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED && ret.videoInfo.BitsPerComponent() == 10,
    };

    if (SUCCEEDED(CheckVideoInfo2Type(&mediaType))) {
        const VIDEOINFOHEADER2 *vih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType.pbFormat);

        if (vih2->dwPictAspectRatioY > 0) {
            /*
             * pixel aspect ratio = display aspect ratio (DAR) / storage aspect ratio (SAR)
             * DAR comes from VIDEOINFOHEADER2.dwPictAspectRatioX / VIDEOINFOHEADER2.dwPictAspectRatioY
             * SAR comes from info.videoInfo.width / info.videoInfo.height
             */
            ret.pixelAspectRatioNum = vih2->dwPictAspectRatioX * ret.videoInfo.height;
            ret.pixelAspectRatioDen = vih2->dwPictAspectRatioY * ret.videoInfo.width;
            CoprimeIntegers(ret.pixelAspectRatioNum, ret.pixelAspectRatioDen);
        }

        if ((vih2->dwControlFlags & AMCONTROL_USED) && (vih2->dwControlFlags & AMCONTROL_COLORINFO_PRESENT)) {
            ret.colorSpaceInfo.Update(reinterpret_cast<const DXVA_ExtendedFormat &>(vih2->dwControlFlags));
        }
    }

    return ret;
}

auto Format::WriteSample(const VideoFormat &videoFormat, const PVideoFrame &srcFrame, BYTE *dstBuffer) -> void {
    const std::array srcSlices { srcFrame->GetReadPtr(PLANAR_Y), srcFrame->GetReadPtr(PLANAR_U), srcFrame->GetReadPtr(PLANAR_V) };
    const std::array srcStrides { srcFrame->GetPitch(PLANAR_Y), srcFrame->GetPitch(PLANAR_U), srcFrame->GetPitch(PLANAR_V) };

    CopyToOutput(videoFormat, srcSlices, srcStrides, dstBuffer, srcFrame->GetRowSize(), srcFrame->GetHeight());
}

auto Format::CreateFrame(const VideoFormat &videoFormat, const BYTE *srcBuffer) -> PVideoFrame {
    PVideoFrame newFrame = AVSF_AVS_API->NewVideoFrame(videoFormat.videoInfo, static_cast<int>(_vectorSize));

    const std::array dstSlices { newFrame->GetWritePtr(PLANAR_Y), newFrame->GetWritePtr(PLANAR_U), newFrame->GetWritePtr(PLANAR_V) };
    const std::array dstStrides { newFrame->GetPitch(PLANAR_Y), newFrame->GetPitch(PLANAR_U), newFrame->GetPitch(PLANAR_V) };

    CopyFromInput(videoFormat, srcBuffer, dstSlices, dstStrides, newFrame->GetRowSize(), newFrame->GetHeight());

    return newFrame;
}

auto Format::CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 3> &dstSlices, const std::array<int, 3> &dstStrides, int frameWidth, int height) -> void {
    const int srcMainPlaneRowSize = frameWidth;
    // bmi.biWidth should be "set equal to the surface stride in pixels" according to the doc of BITMAPINFOHEADER
    int srcMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.ComponentSize() * videoFormat.pixelFormat->componentsPerPixel;
    ASSERT(srcMainPlaneRowSize <= srcMainPlaneStride);
    ASSERT(height == abs(videoFormat.bmi.biHeight));
    const int srcMainPlaneSize = srcMainPlaneStride * height;
    const BYTE *srcMainPlane = srcBuffer;
    const int srcUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio;

    // for RGB DIB in Windows (biCompression == BI_RGB), positive biHeight is bottom-up, negative is top-down
    // AviSynth+'s conversion functions assume input DIB being bottom-up, so we invert the DIB if it's needed
    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        srcMainPlane += static_cast<size_t>(srcMainPlaneSize) - srcMainPlaneStride;
        srcMainPlaneStride = -srcMainPlaneStride;
    }

    if ((videoFormat.pixelFormat->srcPlanesLayout == PlanesLayout::ALL_PLANES_INTERLEAVED && videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_INTERLEAVED) ||
        (videoFormat.pixelFormat->srcPlanesLayout != PlanesLayout::ALL_PLANES_INTERLEAVED && videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_PLANAR)) {
        AVSF_AVS_API->BitBlt(dstSlices[0], dstStrides[0], srcMainPlane, srcMainPlaneStride, srcMainPlaneRowSize, height);
    }

    switch (videoFormat.pixelFormat->srcPlanesLayout) {
    case PlanesLayout::ALL_PLANES_INTERLEAVED:
        if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_PLANAR) {
            const std::array yuvaSlices { dstSlices[1], dstSlices[0], dstSlices[2] };
            const std::array yuvaStrides { dstStrides[1], dstStrides[0], dstStrides[2] };

            if (videoFormat.videoInfo.BitsPerComponent() == 10) {
                DeinterleaveY410(srcMainPlane, srcMainPlaneStride / 2, yuvaSlices, yuvaStrides, srcMainPlaneRowSize * 2, height);
            } else {
                _deinterleaveY416Func(srcMainPlane, srcMainPlaneStride, yuvaSlices, yuvaStrides, srcMainPlaneRowSize * 4, height);
            }
        }
        break;

    case PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED: {
        const BYTE *srcUVStart = srcMainPlane + srcMainPlaneSize;
        const int srcUVStride = srcMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int srcUVRowSize = srcMainPlaneRowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(Deinterleave<0, 1, 2, 2, 1>) *deinterleaveUVFunc;
        if (videoFormat.videoInfo.ComponentSize() == 1) {
            deinterleaveUVFunc = _deinterleaveUVC1Func;
        } else {
            deinterleaveUVFunc = _deinterleaveUVC2Func;
        }
        deinterleaveUVFunc(srcUVStart, srcUVStride, { dstSlices[1], dstSlices[2] }, { dstStrides[1], dstStrides[2] }, srcUVRowSize, srcUVHeight);

        if (videoFormat.videoInfo.BitsPerComponent() == 10) {
            _rightShiftFunc(dstSlices[0], dstSlices[0], dstStrides[0], srcMainPlaneRowSize, height);
            _rightShiftFunc(dstSlices[1], dstSlices[1], dstStrides[1], srcUVRowSize / 2, srcUVHeight);
            _rightShiftFunc(dstSlices[2], dstSlices[2], dstStrides[2], srcUVRowSize / 2, srcUVHeight);
        }
    } break;

    case PlanesLayout::ALL_PLANES_SEPARATE: {
        const int srcUVRowSize = srcMainPlaneRowSize / videoFormat.pixelFormat->subsampleWidthRatio;
        const int srcUVStride = srcMainPlaneStride / videoFormat.pixelFormat->subsampleWidthRatio;
        const BYTE *srcUVPlane1 = srcMainPlane + srcMainPlaneSize;
        const BYTE *srcUVPlane2 = srcUVPlane1 + srcMainPlaneSize / (videoFormat.pixelFormat->subsampleWidthRatio * videoFormat.pixelFormat->subsampleHeightRatio);

        const BYTE *srcU;
        const BYTE *srcV;
        if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_VPlaneFirst) {
            srcU = srcUVPlane2;
            srcV = srcUVPlane1;
        } else {
            srcU = srcUVPlane1;
            srcV = srcUVPlane2;
        }

        AVSF_AVS_API->BitBlt(dstSlices[1], dstStrides[1], srcU, srcUVStride, srcUVRowSize, srcUVHeight);
        AVSF_AVS_API->BitBlt(dstSlices[2], dstStrides[2], srcV, srcUVStride, srcUVRowSize, srcUVHeight);
    } break;
    }
}

auto Format::CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 3> &srcSlices, const std::array<int, 3> &srcStrides, BYTE *dstBuffer, int frameWidth, int height) -> void {
    const int dstMainPlaneRowSize = frameWidth;
    int dstMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.ComponentSize() * videoFormat.pixelFormat->componentsPerPixel;
    ASSERT(dstMainPlaneRowSize <= dstMainPlaneStride);
    ASSERT(height >= abs(videoFormat.bmi.biHeight));
    const int dstMainPlaneSize = dstMainPlaneStride * height;
    const int dstUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio;

    BYTE *intermediateDstBufferBase = dstBuffer;
    if (videoFormat.outputBufferTemporalFlags == 0b111) {
        intermediateDstBufferBase = reinterpret_cast<BYTE *>(CoTaskMemAlloc(videoFormat.bmi.biSizeImage));
    }
    BYTE *dstMainPlane = intermediateDstBufferBase;

    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        dstMainPlane += static_cast<size_t>(dstMainPlaneSize) - dstMainPlaneStride;
        dstMainPlaneStride = -dstMainPlaneStride;
    }

    if ((videoFormat.pixelFormat->srcPlanesLayout == PlanesLayout::ALL_PLANES_INTERLEAVED && videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_INTERLEAVED) ||
        (videoFormat.pixelFormat->srcPlanesLayout != PlanesLayout::ALL_PLANES_INTERLEAVED && videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_PLANAR)) {
        AVSF_AVS_API->BitBlt(dstMainPlane, dstMainPlaneStride, srcSlices[0], srcStrides[0], dstMainPlaneRowSize, height);
    }

    switch (videoFormat.pixelFormat->srcPlanesLayout) {
    case PlanesLayout::ALL_PLANES_INTERLEAVED:
        if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_PLANAR) {
            const std::array yuvaSlices { srcSlices[1], srcSlices[0], srcSlices[2] };
            const std::array yuvaStrides { srcStrides[1], srcStrides[0], srcStrides[2] };

            if (videoFormat.videoInfo.BitsPerComponent() == 10) {
                InterleaveY410(yuvaSlices, yuvaStrides, dstMainPlane, dstMainPlaneStride / 2, dstMainPlaneRowSize * 2, height);
            } else {
                _interleaveY416Func(yuvaSlices, yuvaStrides, dstMainPlane, dstMainPlaneStride, dstMainPlaneRowSize * 4, height);
            }
        }
        break;

    case PlanesLayout::MAIN_SEPARATE_SEC_INTERLEAVED: {
        const int dstUVRowSize = dstMainPlaneRowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        BYTE *dstUVStart = dstMainPlane + dstMainPlaneSize;
        const int dstUVStride = dstMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(InterleaveUV<0, 1>) *interleaveUVFunc;
        if (videoFormat.videoInfo.ComponentSize() == 1) {
            interleaveUVFunc = _interleaveUVC1Func;
        } else {
            interleaveUVFunc = _interleaveUVC2Func;
        }
        interleaveUVFunc(srcSlices[1], srcSlices[2], srcStrides[1], srcStrides[2], dstUVStart, dstUVStride, dstUVRowSize, dstUVHeight);

        if (videoFormat.videoInfo.BitsPerComponent() == 10) {
            _leftShiftFunc(dstMainPlane, dstBuffer, dstMainPlaneStride, dstMainPlaneRowSize, height + dstUVHeight);
            if (videoFormat.outputBufferTemporalFlags == 0b111) {
                CoTaskMemFree(intermediateDstBufferBase);
            }
        }
    } break;

    case PlanesLayout::ALL_PLANES_SEPARATE: {
        const int dstUVRowSize = dstMainPlaneRowSize / videoFormat.pixelFormat->subsampleWidthRatio;
        BYTE *dstUVPlane1 = dstMainPlane + dstMainPlaneSize;
        BYTE *dstUVPlane2 = dstUVPlane1 + dstMainPlaneSize / (videoFormat.pixelFormat->subsampleWidthRatio * videoFormat.pixelFormat->subsampleHeightRatio);
        const int dstUVStride = dstMainPlaneStride / videoFormat.pixelFormat->subsampleWidthRatio;

        BYTE *dstU;
        BYTE *dstV;
        if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_VPlaneFirst) {
            dstU = dstUVPlane2;
            dstV = dstUVPlane1;
        } else {
            dstU = dstUVPlane1;
            dstV = dstUVPlane2;
        }

        AVSF_AVS_API->BitBlt(dstU, dstUVStride, srcSlices[1], srcStrides[1], dstUVRowSize, dstUVHeight);
        AVSF_AVS_API->BitBlt(dstV, dstUVStride, srcSlices[2], srcStrides[2], dstUVRowSize, dstUVHeight);
    } break;
    }
}

}