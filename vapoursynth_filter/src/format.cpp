// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "format.h"

#include "constants.h"
#include "environment.h"
#include "frameserver.h"


namespace SynthFilter {

// for each group of formats with the same format ID, they should appear with the most preferred -> least preferred order
const std::vector<Format::PixelFormat> Format::PIXEL_FORMATS {
    // 4:2:0
    { .name = L"NV12",  .mediaSubtype = MEDIASUBTYPE_NV12,  .frameServerFormatId = pfYUV420P8,  .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_NV12 },
    { .name = L"YV12",  .mediaSubtype = MEDIASUBTYPE_YV12,  .frameServerFormatId = pfYUV420P8,  .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV12 },
    { .name = L"I420",  .mediaSubtype = MEDIASUBTYPE_I420,  .frameServerFormatId = pfYUV420P8,  .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_I420 },
    { .name = L"IYUV",  .mediaSubtype = MEDIASUBTYPE_IYUV,  .frameServerFormatId = pfYUV420P8,  .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_IYUV },

    { .name = L"P016",  .mediaSubtype = MEDIASUBTYPE_P016,  .frameServerFormatId = pfYUV420P16, .bitCount = 24, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P016 },
    // P010 from DirectShow has the least significant 6 bits zero-padded, while AviSynth expects the most significant bits zeroed
    // Therefore, there will be bit shifting whenever P010 is used
    { .name = L"P010",  .mediaSubtype = MEDIASUBTYPE_P010,  .frameServerFormatId = pfYUV420P10, .bitCount = 24, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P010 },

    // 4:2:2
    // VapourSynth does not support YUY2 format
    { .name = L"P216",  .mediaSubtype = MEDIASUBTYPE_P216,  .frameServerFormatId = pfYUV422P16, .bitCount = 32, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P216 },
    // Same note as P010
    { .name = L"P210",  .mediaSubtype = MEDIASUBTYPE_P210,  .frameServerFormatId = pfYUV422P10, .bitCount = 32, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P210 },

    // 4:4:4
    { .name = L"YV24",  .mediaSubtype = MEDIASUBTYPE_YV24,  .frameServerFormatId = pfYUV444P8,  .bitCount = 24, .subsampleWidthRatio = 1, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV24 },

    // VapourSynth does not support packed RGB formats
};

auto Format::GetVideoFormat(const AM_MEDIA_TYPE &mediaType, const FrameServerBase *frameServerInstance) -> VideoFormat {
    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    REFERENCE_TIME fpsNum = UNITS;
    REFERENCE_TIME fpsDen = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;
    CoprimeIntegers(fpsNum, fpsDen);

    VideoFormat ret {
        .pixelFormat = LookupMediaSubtype(mediaType.subtype),
        .pixelAspectRatioNum = 1,
        .pixelAspectRatioDen = 1,
        .hdrType = 0,
        .hdrLuminance = 0,
        .bmi = *GetBitmapInfo(mediaType),
        .frameServerCore = frameServerInstance->GetVsCore(),
    };
    ret.videoInfo = {
        .fpsNum = fpsNum,
        .fpsDen = fpsDen,
        .width = ret.bmi.biWidth,
        .height = abs(ret.bmi.biHeight),
        .numFrames = NUM_FRAMES_FOR_INFINITE_STREAM,
    };
    AVSF_VPS_API->getVideoFormatByID(&ret.videoInfo.format, ret.pixelFormat->frameServerFormatId, ret.frameServerCore);

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

auto Format::WriteSample(const VideoFormat &videoFormat, const VSFrame *srcFrame, BYTE *dstBuffer) -> void {
    const std::array<const BYTE *, 4> srcSlices {
        AVSF_VPS_API->getReadPtr(srcFrame, 0),
        videoFormat.videoInfo.format.numPlanes < 2 ? nullptr : AVSF_VPS_API->getReadPtr(srcFrame, 1),
        videoFormat.videoInfo.format.numPlanes < 3 ? nullptr : AVSF_VPS_API->getReadPtr(srcFrame, 2),
    };
    const std::array<int, 4> srcStrides {
        static_cast<int>(AVSF_VPS_API->getStride(srcFrame, 0)),
        static_cast<int>(videoFormat.videoInfo.format.numPlanes < 2 ? 0 : AVSF_VPS_API->getStride(srcFrame, 1)),
        static_cast<int>(videoFormat.videoInfo.format.numPlanes < 3 ? 0 : AVSF_VPS_API->getStride(srcFrame, 2)),
    };
    const int rowSize = AVSF_VPS_API->getFrameWidth(srcFrame, 0) * videoFormat.videoInfo.format.bytesPerSample;

    CopyToOutput(videoFormat, srcSlices, srcStrides, dstBuffer, rowSize, AVSF_VPS_API->getFrameHeight(srcFrame, 0));
}

auto Format::CreateFrame(const VideoFormat &videoFormat, const BYTE *srcBuffer) -> VSFrame * {
    VSFrame *frame = AVSF_VPS_API->newVideoFrame(&videoFormat.videoInfo.format, videoFormat.videoInfo.width, videoFormat.videoInfo.height, nullptr, videoFormat.frameServerCore);

    const std::array<BYTE *, 4> dstSlices {
        AVSF_VPS_API->getWritePtr(frame, 0),
        videoFormat.videoInfo.format.numPlanes < 2 ? nullptr : AVSF_VPS_API->getWritePtr(frame, 1),
        videoFormat.videoInfo.format.numPlanes < 3 ? nullptr : AVSF_VPS_API->getWritePtr(frame, 2),
    };
    const std::array<int, 4> dstStrides {
        static_cast<int>(AVSF_VPS_API->getStride(frame, 0)),
        static_cast<int>(videoFormat.videoInfo.format.numPlanes < 2 ? 0 : AVSF_VPS_API->getStride(frame, 1)),
        static_cast<int>(videoFormat.videoInfo.format.numPlanes < 3 ? 0 : AVSF_VPS_API->getStride(frame, 2)),
    };
    const int rowSize = AVSF_VPS_API->getFrameWidth(frame, 0) * videoFormat.videoInfo.format.bytesPerSample;

    CopyFromInput(videoFormat, srcBuffer, dstSlices, dstStrides, rowSize, AVSF_VPS_API->getFrameHeight(frame, 0));

    return frame;
}

auto Format::CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 4> &dstSlices, const std::array<int, 4> &dstStrides, int rowSize, int height) -> void {
    // bmi.biWidth should be "set equal to the surface stride in pixels" according to the doc of BITMAPINFOHEADER
    int srcMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.format.bytesPerSample;
    ASSERT(rowSize <= srcMainPlaneStride);
    ASSERT(height == abs(videoFormat.bmi.biHeight));
    const int srcMainPlaneSize = srcMainPlaneStride * height;
    const BYTE *srcMainPlane = srcBuffer;

    // for RGB DIB in Windows (biCompression == BI_RGB), positive biHeight is bottom-up, negative is top-down
    // AviSynth+'s convert functions always assume the input DIB is bottom-up, so we invert the DIB if it's top-down
    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        srcMainPlane += static_cast<size_t>(srcMainPlaneSize) - srcMainPlaneStride;
        srcMainPlaneStride = -srcMainPlaneStride;
    }

    vsh::bitblt(dstSlices[0], dstStrides[0], srcMainPlane, srcMainPlaneStride, rowSize, height);

    if (const int srcUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        const BYTE *srcUVStart = srcBuffer + srcMainPlaneSize;
        const int srcUVStride = srcMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int srcUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(Deinterleave<0, 0, 2>) *DeinterleaveFunc;
        if (videoFormat.videoInfo.format.bytesPerSample == 1) {
            if (Environment::GetInstance().IsSupportAVXx()) {
                DeinterleaveFunc = Deinterleave<2, 1, 2>;
            } else if (Environment::GetInstance().IsSupportSSSE3()) {
                DeinterleaveFunc = Deinterleave<1, 1, 2>;
            } else {
                DeinterleaveFunc = Deinterleave<0, 1, 2>;
            }
        } else {
            if (Environment::GetInstance().IsSupportAVXx()) {
                DeinterleaveFunc = Deinterleave<2, 2, 2>;
            } else if (Environment::GetInstance().IsSupportSSSE3()) {
                DeinterleaveFunc = Deinterleave<1, 2, 2>;
            } else {
                DeinterleaveFunc = Deinterleave<0, 2, 2>;
            }
        }
        DeinterleaveFunc(srcUVStart, srcUVStride, { dstSlices[1], dstSlices[2] }, { dstStrides[1], dstStrides[2] }, srcUVRowSize, srcUVHeight);

        if (videoFormat.videoInfo.format.bitsPerSample == 10) {
            decltype(BitShiftEach16BitInt<0, 0, true>) *RightShiftFunc;
            if (Environment::GetInstance().IsSupportAVXx()) {
                RightShiftFunc = BitShiftEach16BitInt<2, 6, true>;
            } else {
                RightShiftFunc = BitShiftEach16BitInt<1, 6, true>;
            }

            RightShiftFunc(dstSlices[0], dstStrides[0], rowSize, height);
            RightShiftFunc(dstSlices[1], dstStrides[1], srcUVRowSize / 2, srcUVHeight);
            RightShiftFunc(dstSlices[2], dstStrides[2], srcUVRowSize / 2, srcUVHeight);
        }
    } else {
        const int srcUVStride = srcMainPlaneStride / videoFormat.pixelFormat->subsampleWidthRatio;
        const int srcUVRowSize = rowSize / videoFormat.pixelFormat->subsampleWidthRatio;
        const BYTE *srcUVPlane1 = srcBuffer + srcMainPlaneSize;
        const BYTE *srcUVPlane2 = srcUVPlane1 + srcMainPlaneSize / (videoFormat.pixelFormat->subsampleWidthRatio * videoFormat.pixelFormat->subsampleHeightRatio);

        const BYTE *srcU;
        const BYTE *srcV;
        if (videoFormat.pixelFormat->mediaSubtype == MEDIASUBTYPE_YV12 || videoFormat.pixelFormat->mediaSubtype == MEDIASUBTYPE_YV24) {
            // YVxx has V plane first

            srcU = srcUVPlane2;
            srcV = srcUVPlane1;
        } else {
            srcU = srcUVPlane1;
            srcV = srcUVPlane2;
        }

        vsh::bitblt(dstSlices[1], dstStrides[1], srcU, srcUVStride, srcUVRowSize, srcUVHeight);
        vsh::bitblt(dstSlices[2], dstStrides[2], srcV, srcUVStride, srcUVRowSize, srcUVHeight);
    }
}

auto Format::CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 4> &srcSlices, const std::array<int, 4> &srcStrides, BYTE *dstBuffer, int rowSize, int height) -> void {
    int dstMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.format.bytesPerSample;
    ASSERT(rowSize <= dstMainPlaneStride);
    ASSERT(height >= abs(videoFormat.bmi.biHeight));
    const int dstMainPlaneSize = dstMainPlaneStride * height;
    BYTE *dstMainPlane = dstBuffer;

    // AviSynth+'s convert functions always produce bottom-up DIB, so we invert the DIB if downstream needs top-down
    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        dstMainPlane += static_cast<size_t>(dstMainPlaneSize) - dstMainPlaneStride;
        dstMainPlaneStride = -dstMainPlaneStride;
    }

    vsh::bitblt(dstMainPlane, dstMainPlaneStride, srcSlices[0], srcStrides[0], rowSize, height);

    if (const int dstUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        BYTE *dstUVStart = dstBuffer + dstMainPlaneSize;
        const int dstUVStride = dstMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int dstUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(InterleaveUV<0, 0>) *InterleaveUVFunc;
        if (videoFormat.videoInfo.format.bytesPerSample == 1) {
            if (Environment::GetInstance().IsSupportAVXx()) {
                InterleaveUVFunc = InterleaveUV<2, 1>;
            } else {
                InterleaveUVFunc = InterleaveUV<1, 1>;
            }
        } else {
            if (Environment::GetInstance().IsSupportAVXx()) {
                InterleaveUVFunc = InterleaveUV<2, 2>;
            } else {
                InterleaveUVFunc = InterleaveUV<1, 2>;
            }
        }
        InterleaveUVFunc(srcSlices[1], srcSlices[2], srcStrides[1], srcStrides[2], dstUVStart, dstUVStride, dstUVRowSize, dstUVHeight);

        if (videoFormat.videoInfo.format.bitsPerSample == 10) {
            decltype(BitShiftEach16BitInt<0, 0, false>) *LeftShiftFunc;
            if (Environment::GetInstance().IsSupportAVXx()) {
                LeftShiftFunc = BitShiftEach16BitInt<2, 6, false>;
            } else {
                LeftShiftFunc = BitShiftEach16BitInt<1, 6, false>;
            }

            LeftShiftFunc(dstMainPlane, dstMainPlaneStride, rowSize, height);
            LeftShiftFunc(dstUVStart, dstUVStride, dstUVRowSize, dstUVHeight);
        }
    } else {
        const int dstUVStride = dstMainPlaneStride / videoFormat.pixelFormat->subsampleWidthRatio;
        const int dstUVRowSize = rowSize / videoFormat.pixelFormat->subsampleWidthRatio;
        BYTE *dstUVPlane1 = dstBuffer + dstMainPlaneSize;
        BYTE *dstUVPlane2 = dstUVPlane1 + dstMainPlaneSize / (videoFormat.pixelFormat->subsampleWidthRatio * videoFormat.pixelFormat->subsampleHeightRatio);

        BYTE *dstU;
        BYTE *dstV;
        if (videoFormat.pixelFormat->mediaSubtype == MEDIASUBTYPE_YV12 || videoFormat.pixelFormat->mediaSubtype == MEDIASUBTYPE_YV24) {
            dstU = dstUVPlane2;
            dstV = dstUVPlane1;
        } else {
            dstU = dstUVPlane1;
            dstV = dstUVPlane2;
        }

        vsh::bitblt(dstU, dstUVStride, srcSlices[1], srcStrides[1], dstUVRowSize, dstUVHeight);
        vsh::bitblt(dstV, dstUVStride, srcSlices[2], srcStrides[2], dstUVRowSize, dstUVHeight);
    }
}

}
