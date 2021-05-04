// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "format.h"
#include "api.h"
#include "frameserver.h"
#include "constants.h"
#include "environment.h"


namespace SynthFilter {

// for each group of formats with the same format ID, they should appear with the most preferred -> list preferred order
const std::vector<Format::PixelFormat> Format::PIXEL_FORMATS = {
    // 4:2:0
    { .name = L"NV12",  .mediaSubtype = MEDIASUBTYPE_NV12,  .fsFormatId = pfYUV420P8,    .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_NV12 },
    { .name = L"YV12",  .mediaSubtype = MEDIASUBTYPE_YV12,  .fsFormatId = pfYUV420P8,    .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV12 },
    { .name = L"I420",  .mediaSubtype = MEDIASUBTYPE_I420,  .fsFormatId = pfYUV420P8,    .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_I420 },
    { .name = L"IYUV",  .mediaSubtype = MEDIASUBTYPE_IYUV,  .fsFormatId = pfYUV420P8,    .bitCount = 12, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_IYUV },

    // P010 has the most significant 6 bits zero-padded, while VapourSynth expects the least significant bits padded
    // P010 without right shifting 6 bits on every WORD is equivalent to P016, without precision loss
    { .name = L"P016",  .mediaSubtype = MEDIASUBTYPE_P016,  .fsFormatId = pfYUV420P16,   .bitCount = 24, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P016 },
    { .name = L"P010",  .mediaSubtype = MEDIASUBTYPE_P010,  .fsFormatId = pfYUV420P16,   .bitCount = 24, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P010 },

    // 4:2:2
    { .name = L"YUY2",  .mediaSubtype = MEDIASUBTYPE_YUY2,  .fsFormatId = pfCompatYUY2,  .bitCount = 16, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YUY2 },
    // P210 has the same problem as P010
    { .name = L"P216",  .mediaSubtype = MEDIASUBTYPE_P216,  .fsFormatId = pfYUV422P16,   .bitCount = 32, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P216 },
    { .name = L"P210",  .mediaSubtype = MEDIASUBTYPE_P210,  .fsFormatId = pfYUV422P16,   .bitCount = 32, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P210 },

    // 4:4:4
    { .name = L"YV24",  .mediaSubtype = MEDIASUBTYPE_YV24,  .fsFormatId = pfYUV444P8,    .bitCount = 24, .subsampleWidthRatio = 1, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV24 },

    // RGB
    { .name = L"RGB32", .mediaSubtype = MEDIASUBTYPE_RGB32, .fsFormatId = pfCompatBGR32, .bitCount = 32, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_RGB32 },
    // RGB48 will not work because LAV Filters outputs R-G-B pixel order while AviSynth+ expects B-G-R
};

auto Format::VideoFormat::operator!=(const VideoFormat &other) const -> bool {
    return pixelFormat!= other.pixelFormat
        || memcmp(&videoInfo, &other.videoInfo, sizeof(videoInfo)) != 0
        || pixelAspectRatio != other.pixelAspectRatio
        || hdrType != other.hdrType
        || hdrLuminance != other.hdrLuminance
        || bmi.biSize != other.bmi.biSize
        || memcmp(&bmi, &other.bmi, bmi.biSize) != 0;
}

auto Format::VideoFormat::GetCodecFourCC() const -> DWORD {
    return FOURCCMap(&pixelFormat->mediaSubtype).GetFOURCC();
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

auto Format::GetVideoFormat(const AM_MEDIA_TYPE &mediaType, const FrameServerBase *scriptInstance) -> VideoFormat {
    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    REFERENCE_TIME fpsNum = UNITS;
    REFERENCE_TIME frameDuration = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;
    vs_normalizeRational(&fpsNum, &frameDuration);
    VSCore *vsCore= vsscript_getCore(scriptInstance->GetVsScript());

    VideoFormat ret {
        .pixelFormat = LookupMediaSubtype(mediaType.subtype),
        .pixelAspectRatio = PAR_SCALE_FACTOR,
        .hdrType = 0,
        .hdrLuminance = 0,
        .bmi = *GetBitmapInfo(mediaType),
        .fsEnvironment = vsCore
    };
    ret.videoInfo = {
        .format = AVSF_VS_API->getFormatPreset(ret.pixelFormat->fsFormatId, ret.fsEnvironment),
        .fpsNum = fpsNum,
        .fpsDen = frameDuration,
        .width = ret.bmi.biWidth,
        .height = abs(ret.bmi.biHeight),
        .numFrames = NUM_FRAMES_FOR_INFINITE_STREAM,
        .flags = nfNoCache,
    };

    if (SUCCEEDED(CheckVideoInfo2Type(&mediaType))) {
        if (const VIDEOINFOHEADER2* vih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType.pbFormat); vih2->dwPictAspectRatioY > 0) {
            /*
             * pixel aspect ratio = display aspect ratio (DAR) / storage aspect ratio (SAR)
             * DAR comes from VIDEOINFOHEADER2.dwPictAspectRatioX / VIDEOINFOHEADER2.dwPictAspectRatioY
             * SAR comes from info.videoInfo.width / info.videoInfo.height
             */
            ret.pixelAspectRatio = static_cast<int>(llMulDiv(static_cast<LONGLONG>(vih2->dwPictAspectRatioX) * ret.videoInfo.height,
                                                    PAR_SCALE_FACTOR,
                                                    static_cast<LONGLONG>(vih2->dwPictAspectRatioY) * ret.videoInfo.width,
                                                    0));
        }
    }

    return ret;
}

auto Format::WriteSample(const VideoFormat &videoFormat, const VSFrameRef *srcFrame, BYTE *dstBuffer) -> void {
    const std::array srcSlices = { AVSF_VS_API->getReadPtr(srcFrame, 0)
                                 , videoFormat.videoInfo.format->numPlanes < 2 ? nullptr : AVSF_VS_API->getReadPtr(srcFrame, 1)
                                 , videoFormat.videoInfo.format->numPlanes < 3 ? nullptr : AVSF_VS_API->getReadPtr(srcFrame, 2) };
    const std::array srcStrides = { AVSF_VS_API->getStride(srcFrame, 0)
                                  , videoFormat.videoInfo.format->numPlanes < 2 ? 0 : AVSF_VS_API->getStride(srcFrame, 1)
                                  , videoFormat.videoInfo.format->numPlanes < 3 ? 0 : AVSF_VS_API->getStride(srcFrame, 2) };
    const int rowSize = AVSF_VS_API->getFrameWidth(srcFrame, 0) * videoFormat.videoInfo.format->bytesPerSample;

    CopyToOutput(videoFormat, srcSlices, srcStrides, dstBuffer, rowSize, AVSF_VS_API->getFrameHeight(srcFrame, 0));
}

auto Format::CreateFrame(const VideoFormat &videoFormat, const BYTE *srcBuffer) -> const VSFrameRef * {
    VSFrameRef *frame = AVSF_VS_API->newVideoFrame(videoFormat.videoInfo.format, videoFormat.videoInfo.width, videoFormat.videoInfo.height, nullptr, videoFormat.fsEnvironment);

    const std::array dstSlices = { AVSF_VS_API->getWritePtr(frame, 0)
                                 , videoFormat.videoInfo.format->numPlanes < 2 ? nullptr : AVSF_VS_API->getWritePtr(frame, 1)
                                 , videoFormat.videoInfo.format->numPlanes < 3 ? nullptr : AVSF_VS_API->getWritePtr(frame, 2) };
    const std::array dstStrides = { AVSF_VS_API->getStride(frame, 0)
                                  , videoFormat.videoInfo.format->numPlanes < 2 ? 0 : AVSF_VS_API->getStride(frame, 1)
                                  , videoFormat.videoInfo.format->numPlanes < 3 ? 0 : AVSF_VS_API->getStride(frame, 2) };
    const int rowSize = AVSF_VS_API->getFrameWidth(frame, 0) * videoFormat.videoInfo.format->bytesPerSample;

    CopyFromInput(videoFormat, srcBuffer, dstSlices, dstStrides, rowSize, AVSF_VS_API->getFrameHeight(frame, 0));

    return frame;
}

auto Format::CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 3> &dstSlices, const std::array<int, 3> &dstStrides, int rowSize, int height) -> void {
    // bmi.biWidth should be "set equal to the surface stride in pixels" according to the doc of BITMAPINFOHEADER
    int srcMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.format->bytesPerSample;
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

    vs_bitblt(dstSlices[0], dstStrides[0], srcMainPlane, srcMainPlaneStride, rowSize, height);

    if (videoFormat.pixelFormat->fsFormatId >= cmCompat) {
        return;
    }

    if (const int srcUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        const BYTE *srcUVStart = srcBuffer + srcMainPlaneSize;
        const int srcUVStride = srcMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int srcUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(Deinterleave<0, 0>)* DeinterleaveFunc;
        if (videoFormat.videoInfo.format->bytesPerSample == 1) {
            if (Environment::GetInstance().IsSupportAVXx()) {
                DeinterleaveFunc = Deinterleave<2, 1>;
            } else if (Environment::GetInstance().IsSupportSSSE3()) {
                DeinterleaveFunc = Deinterleave<1, 1>;
            } else {
                DeinterleaveFunc = Deinterleave<0, 1>;
            }
        } else {
            if (Environment::GetInstance().IsSupportAVXx()) {
                DeinterleaveFunc = Deinterleave<2, 2>;
            } else if (Environment::GetInstance().IsSupportSSSE3()) {
                DeinterleaveFunc = Deinterleave<1, 2>;
            } else {
                DeinterleaveFunc = Deinterleave<0, 2>;
            }
        }
        DeinterleaveFunc(srcUVStart, srcUVStride, dstSlices[1], dstSlices[2], dstStrides[1], srcUVRowSize, srcUVHeight);
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

        vs_bitblt(dstSlices[1], dstStrides[1], srcU, srcUVStride, srcUVRowSize, srcUVHeight);
        vs_bitblt(dstSlices[2], dstStrides[2], srcV, srcUVStride, srcUVRowSize, srcUVHeight);
    }
}

auto Format::CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 3> &srcSlices, const std::array<int, 3> &srcStrides, BYTE *dstBuffer, int rowSize, int height) -> void {
    int dstMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.format->bytesPerSample;
    ASSERT(rowSize <= dstMainPlaneStride);
    ASSERT(height >= abs(videoFormat.bmi.biHeight));
    const int dstMainPlaneSize = dstMainPlaneStride * height;
    BYTE *dstMainPlane = dstBuffer;

    // AviSynth+'s convert functions always produce bottom-up DIB, so we invert the DIB if downstream needs top-down
    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        dstMainPlane += static_cast<size_t>(dstMainPlaneSize) - dstMainPlaneStride;
        dstMainPlaneStride = -dstMainPlaneStride;
    }

    vs_bitblt(dstMainPlane, dstMainPlaneStride, srcSlices[0], srcStrides[0], rowSize, height);

    if (videoFormat.pixelFormat->fsFormatId >= cmCompat) {
        return;
    }

    if (const int dstUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        BYTE *dstUVStart = dstBuffer + dstMainPlaneSize;
        const int dstUVStride = dstMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int dstUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(Interleave<0, 0>)* InterleaveFunc;
        if (videoFormat.videoInfo.format->bytesPerSample == 1) {
            if (Environment::GetInstance().IsSupportAVXx()) {
                InterleaveFunc = Interleave<2, 1>;
            } else {
                InterleaveFunc = Interleave<1, 1>;
            }
        } else {
            if (Environment::GetInstance().IsSupportAVXx()) {
                InterleaveFunc = Interleave<2, 2>;
            } else {
                InterleaveFunc = Interleave<1, 2>;
            }
        }
        InterleaveFunc(srcSlices[1], srcSlices[2], srcStrides[1], dstUVStart, dstUVStride, dstUVRowSize, dstUVHeight);
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

        vs_bitblt(dstU, dstUVStride, srcSlices[1], srcStrides[1], dstUVRowSize, dstUVHeight);
        vs_bitblt(dstV, dstUVStride, srcSlices[2], srcStrides[2], dstUVRowSize, dstUVHeight);
    }
}

}
