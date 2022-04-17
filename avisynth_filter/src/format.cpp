// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "format.h"

#include "constants.h"
#include "frameserver.h"


namespace SynthFilter {

// for each group of formats with the same format ID, they should appear with the most preferred -> least preferred order
const std::vector<Format::PixelFormat> Format::PIXEL_FORMATS {
    // 4:2:0
    { .name = L"NV12",  .mediaSubtype = MEDIASUBTYPE_NV12,  .frameServerFormatId = VideoInfo::CS_YV12,       .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_NV12 },
    { .name = L"YV12",  .mediaSubtype = MEDIASUBTYPE_YV12,  .frameServerFormatId = VideoInfo::CS_YV12,       .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV12 },
    { .name = L"I420",  .mediaSubtype = MEDIASUBTYPE_I420,  .frameServerFormatId = VideoInfo::CS_YV12,       .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_I420 },
    { .name = L"IYUV",  .mediaSubtype = MEDIASUBTYPE_IYUV,  .frameServerFormatId = VideoInfo::CS_YV12,       .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_IYUV },

    { .name = L"P016",  .mediaSubtype = MEDIASUBTYPE_P016,  .frameServerFormatId = VideoInfo::CS_YUV420P16,  .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P016 },
    // P010 from DirectShow has the least significant 6 bits zero-padded, while AviSynth expects the most significant bits zeroed
    // Therefore, there will be bit shifting whenever P010 is used
    { .name = L"P010",  .mediaSubtype = MEDIASUBTYPE_P010,  .frameServerFormatId = VideoInfo::CS_YUV420P10,  .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P010 },

    // 4:2:2
    // YUY2 interleaves Y and UV planes together, thus twice as wide as unpacked formats per pixel
    { .name = L"YUY2",  .mediaSubtype = MEDIASUBTYPE_YUY2,  .frameServerFormatId = VideoInfo::CS_YUY2,       .bitCount = 16, .componentsPerPixel = 2, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YUY2 },
    // AviSynth+ does not support UYVY
    { .name = L"P216",  .mediaSubtype = MEDIASUBTYPE_P216,  .frameServerFormatId = VideoInfo::CS_YUV422P16,  .bitCount = 32, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P216 },
    // Same note as P010
    { .name = L"P210",  .mediaSubtype = MEDIASUBTYPE_P210,  .frameServerFormatId = VideoInfo::CS_YUV422P10,  .bitCount = 32, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P210 },

    // 4:4:4
    { .name = L"YV24",  .mediaSubtype = MEDIASUBTYPE_YV24,  .frameServerFormatId = VideoInfo::CS_YV24,       .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 1, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV24 },
    { .name = L"Y416",  .mediaSubtype = MEDIASUBTYPE_Y416,  .frameServerFormatId = VideoInfo::CS_YUVA444P16, .bitCount = 64, .componentsPerPixel = 4, .subsampleWidthRatio = 1, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_Y416 },

    // RGB
    { .name = L"RGB24", .mediaSubtype = MEDIASUBTYPE_RGB24, .frameServerFormatId = VideoInfo::CS_BGR24,      .bitCount = 24, .componentsPerPixel = 3, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_RGB24 },
    { .name = L"RGB32", .mediaSubtype = MEDIASUBTYPE_RGB32, .frameServerFormatId = VideoInfo::CS_BGR32,      .bitCount = 32, .componentsPerPixel = 4, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_RGB32 },
    // RGB48 will not work because LAV Filters outputs R-G-B pixel order while AviSynth+ expects B-G-R
};

auto Format::GetVideoFormat(const AM_MEDIA_TYPE &mediaType, const FrameServerBase *frameServerInstance) -> VideoFormat {
    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    const REFERENCE_TIME frameDuration = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;

    VideoFormat ret {
        .pixelFormat = LookupMediaSubtype(mediaType.subtype),
        .pixelAspectRatioNum = 1,
        .pixelAspectRatioDen = 1,
        .hdrType = 0,
        .hdrLuminance = 0,
        .bmi = *GetBitmapInfo(mediaType),
    };
    ret.videoInfo = {
        .width = ret.bmi.biWidth,
        .height = abs(ret.bmi.biHeight),
        .fps_numerator = UNITS,
        .fps_denominator = static_cast<unsigned int>(frameDuration),
        .num_frames = NUM_FRAMES_FOR_INFINITE_STREAM,
        .pixel_type = ret.pixelFormat->frameServerFormatId,
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
        }

        if ((vih2->dwControlFlags & AMCONTROL_USED) && (vih2->dwControlFlags & AMCONTROL_COLORINFO_PRESENT)) {
            ret.colorSpaceInfo.Update(reinterpret_cast<const DXVA_ExtendedFormat &>(vih2->dwControlFlags));
        }
    }

    return ret;
}

auto Format::WriteSample(const VideoFormat &videoFormat, const PVideoFrame &srcFrame, BYTE *dstBuffer) -> void {
    const std::array srcSlices { srcFrame->GetReadPtr(PLANAR_Y), srcFrame->GetReadPtr(PLANAR_U), srcFrame->GetReadPtr(PLANAR_V), srcFrame->GetReadPtr(PLANAR_A) };
    const std::array srcStrides { srcFrame->GetPitch(PLANAR_Y), srcFrame->GetPitch(PLANAR_U), srcFrame->GetPitch(PLANAR_V), srcFrame->GetPitch(PLANAR_A) };

    CopyToOutput(videoFormat, srcSlices, srcStrides, dstBuffer, srcFrame->GetRowSize(), srcFrame->GetHeight());
}

auto Format::CreateFrame(const VideoFormat &videoFormat, const BYTE *srcBuffer) -> PVideoFrame {
    PVideoFrame frame = AVSF_AVS_API->NewVideoFrame(videoFormat.videoInfo, static_cast<int>(_vectorSize));

    const std::array dstSlices { frame->GetWritePtr(PLANAR_Y), frame->GetWritePtr(PLANAR_U), frame->GetWritePtr(PLANAR_V), frame->GetWritePtr(PLANAR_A) };
    const std::array dstStrides { frame->GetPitch(PLANAR_Y), frame->GetPitch(PLANAR_U), frame->GetPitch(PLANAR_V), frame->GetPitch(PLANAR_A) };

    CopyFromInput(videoFormat, srcBuffer, dstSlices, dstStrides, frame->GetRowSize(), frame->GetHeight());

    return frame;
}

static auto DeinterleaveYUVA(const BYTE *src, int srcStride, std::array<BYTE *, 4> dsts, int dstStride, int rowSize, int height) -> void {
    using Vector = __m128i;
    using Output = int32_t;

    const Vector shuffleMask = _mm_setr_epi8(0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15);

    for (int y = 0; y < height; ++y) {
        const Vector *srcLine = reinterpret_cast<const Vector *>(src);
        std::array<Output *, dsts.size()> dstsLine;
        for (size_t p = 0; p < dsts.size(); ++p) {
            dstsLine[p] = reinterpret_cast<Output *>(dsts[p]);
        }

        for (int i = 0; i < DivideRoundUp(static_cast<int>(rowSize * dsts.size()), sizeof(Vector)); ++i) {
            const Vector outputVec = _mm_shuffle_epi8(*srcLine++, shuffleMask);
            for (size_t p = 0; p < dsts.size(); ++p) {
                *dstsLine[p]++ = *(reinterpret_cast<const Output *>(&outputVec) + p);
            }
        }

        src += srcStride;
        for (size_t p = 0; p < dsts.size(); ++p) {
            dsts[p] += dstStride;
        }
    }
}

static auto InterleaveYUVA(std::array<const BYTE *, 4> srcs, int srcStride, BYTE *dst, int dstStride, int rowSize, int height) -> void {
    using Vector = __m128i;
    using Output = int32_t;

    const Vector shuffleMask = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);

    for (int y = 0; y < height; ++y) {
        std::array<const Output *, srcs.size()> srcsLine;
        for (size_t p = 0; p < srcs.size(); ++p) {
            srcsLine[p] = reinterpret_cast<const Output *>(srcs[p]);
        }
        Vector *dstLine = reinterpret_cast<Vector *>(dst);

        for (int i = 0; i < DivideRoundUp(static_cast<int>(rowSize * srcs.size()), sizeof(Vector)); ++i) {
            Vector outputVec = _mm_insert_epi32(_mm_setzero_si128(), *srcsLine[0]++, 0);
            outputVec = _mm_insert_epi32(outputVec, *srcsLine[1]++, 1);
            outputVec = _mm_insert_epi32(outputVec, *srcsLine[2]++, 2);
            outputVec = _mm_insert_epi32(outputVec, *srcsLine[3]++, 3);
            *dstLine++ = _mm_shuffle_epi8(outputVec, shuffleMask);
        }

        for (size_t p = 0; p < srcs.size(); ++p) {
            srcs[p] += srcStride;
        }
        dst += dstStride;
    }
}

auto Format::CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 4> &dstSlices, const std::array<int, 4> &dstStrides, int rowSize, int height) -> void {
    // bmi.biWidth should be "set equal to the surface stride in pixels" according to the doc of BITMAPINFOHEADER
    int srcMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.ComponentSize() * videoFormat.pixelFormat->componentsPerPixel;
    ASSERT(rowSize <= srcMainPlaneStride);
    ASSERT(height == abs(videoFormat.bmi.biHeight));
    const int srcMainPlaneSize = srcMainPlaneStride * height;
    const BYTE *srcMainPlane = srcBuffer;

    if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_YUVA) {
        DeinterleaveYUVA(srcMainPlane, srcMainPlaneStride, { dstSlices[1], dstSlices[0], dstSlices[2], dstSlices[3] }, dstStrides[0], rowSize, height);
        return;
    }

    // for RGB DIB in Windows (biCompression == BI_RGB), positive biHeight is bottom-up, negative is top-down
    // AviSynth+'s convert functions always assume the input DIB is bottom-up, so we invert the DIB if it's top-down
    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        srcMainPlane += static_cast<size_t>(srcMainPlaneSize) - srcMainPlaneStride;
        srcMainPlaneStride = -srcMainPlaneStride;
    }

    AVSF_AVS_API->BitBlt(dstSlices[0], dstStrides[0], srcMainPlane, srcMainPlaneStride, rowSize, height);

    if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_INTERLEAVED) {
        return;
    }

    if (const int srcUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        const BYTE *srcUVStart = srcBuffer + srcMainPlaneSize;
        const int srcUVStride = srcMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int srcUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(DeinterleaveUV<0, 0>) *DeinterleaveUVFunc;
        if (videoFormat.videoInfo.ComponentSize() == 1) {
            if (Environment::GetInstance().IsSupportAVXx()) {
                DeinterleaveUVFunc = DeinterleaveUV<2, 1>;
            } else if (Environment::GetInstance().IsSupportSSSE3()) {
                DeinterleaveUVFunc = DeinterleaveUV<1, 1>;
            } else {
                DeinterleaveUVFunc = DeinterleaveUV<0, 1>;
            }
        } else {
            if (Environment::GetInstance().IsSupportAVXx()) {
                DeinterleaveUVFunc = DeinterleaveUV<2, 2>;
            } else if (Environment::GetInstance().IsSupportSSSE3()) {
                DeinterleaveUVFunc = DeinterleaveUV<1, 2>;
            } else {
                DeinterleaveUVFunc = DeinterleaveUV<0, 2>;
            }
        }
        DeinterleaveUVFunc(srcUVStart, srcUVStride, dstSlices[1], dstSlices[2], dstStrides[1], srcUVRowSize, srcUVHeight);

        if (videoFormat.videoInfo.BitsPerComponent() == 10) {
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

        AVSF_AVS_API->BitBlt(dstSlices[1], dstStrides[1], srcU, srcUVStride, srcUVRowSize, srcUVHeight);
        AVSF_AVS_API->BitBlt(dstSlices[2], dstStrides[2], srcV, srcUVStride, srcUVRowSize, srcUVHeight);
    }
}

auto Format::CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 4> &srcSlices, const std::array<int, 4> &srcStrides, BYTE *dstBuffer, int rowSize, int height) -> void {
    int dstMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.ComponentSize() * videoFormat.pixelFormat->componentsPerPixel;
    ASSERT(rowSize <= dstMainPlaneStride);
    ASSERT(height >= abs(videoFormat.bmi.biHeight));
    const int dstMainPlaneSize = dstMainPlaneStride * height;
    BYTE *dstMainPlane = dstBuffer;

    if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_YUVA) {
        InterleaveYUVA({ srcSlices[1], srcSlices[0], srcSlices[2], srcSlices[3] }, srcStrides[0], dstMainPlane, dstMainPlaneStride, rowSize, height);
        return;
    }

    // AviSynth+'s convert functions always produce bottom-up DIB, so we invert the DIB if downstream needs top-down
    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        dstMainPlane += static_cast<size_t>(dstMainPlaneSize) - dstMainPlaneStride;
        dstMainPlaneStride = -dstMainPlaneStride;
    }

    AVSF_AVS_API->BitBlt(dstMainPlane, dstMainPlaneStride, srcSlices[0], srcStrides[0], rowSize, height);

    if (videoFormat.pixelFormat->frameServerFormatId & VideoInfo::CS_INTERLEAVED) {
        return;
    }

    if (const int dstUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        BYTE *dstUVStart = dstBuffer + dstMainPlaneSize;
        const int dstUVStride = dstMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int dstUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(InterleaveUV<0, 0>) *InterleaveUVFunc;
        if (videoFormat.videoInfo.ComponentSize() == 1) {
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
        InterleaveUVFunc(srcSlices[1], srcSlices[2], srcStrides[1], dstUVStart, dstUVStride, dstUVRowSize, dstUVHeight);

        if (videoFormat.videoInfo.BitsPerComponent() == 10) {
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

        AVSF_AVS_API->BitBlt(dstU, dstUVStride, srcSlices[1], srcStrides[1], dstUVRowSize, dstUVHeight);
        AVSF_AVS_API->BitBlt(dstV, dstUVStride, srcSlices[2], srcStrides[2], dstUVRowSize, dstUVHeight);
    }
}

}
