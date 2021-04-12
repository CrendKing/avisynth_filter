// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "format.h"
#include "api.h"
#include "constants.h"
#include "environment.h"
#include "resource.h"


namespace AvsFilter {

// for each group of formats with the same avsType, they should appear with the most preferred -> list preferred order
const std::vector<Format::PixelFormat> Format::PIXEL_FORMATS = {
    // 4:2:0
    { .name = L"NV12",  .mediaSubtype = MEDIASUBTYPE_NV12,  .avsType = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_NV12 },
    { .name = L"YV12",  .mediaSubtype = MEDIASUBTYPE_YV12,  .avsType = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV12 },
    { .name = L"I420",  .mediaSubtype = MEDIASUBTYPE_I420,  .avsType = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_I420 },
    { .name = L"IYUV",  .mediaSubtype = MEDIASUBTYPE_IYUV,  .avsType = VideoInfo::CS_YV12,      .bitCount = 12, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_IYUV },

    // P010 has the most significant 6 bits zero-padded, while AviSynth expects the least significant bits padded
    // P010 without right shifting 6 bits on every WORD is equivalent to P016, without precision loss
    { .name = L"P016",  .mediaSubtype = MEDIASUBTYPE_P016,  .avsType = VideoInfo::CS_YUV420P16, .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P016 },
    { .name = L"P010",  .mediaSubtype = MEDIASUBTYPE_P010,  .avsType = VideoInfo::CS_YUV420P16, .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 2, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P010 },

    // 4:2:2
    // YUY2 interleaves Y and UV planes together, thus twice as wide as unpacked formats per pixel
    { .name = L"YUY2",  .mediaSubtype = MEDIASUBTYPE_YUY2,  .avsType = VideoInfo::CS_YUY2,      .bitCount = 16, .componentsPerPixel = 2, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YUY2 },
    // AviSynth+ does not support UYVY
    // P210 has the same problem as P010
    { .name = L"P216",  .mediaSubtype = MEDIASUBTYPE_P216,  .avsType = VideoInfo::CS_YUV422P16, .bitCount = 32, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P216 },
    { .name = L"P210",  .mediaSubtype = MEDIASUBTYPE_P210,  .avsType = VideoInfo::CS_YUV422P16, .bitCount = 32, .componentsPerPixel = 1, .subsampleWidthRatio = 2, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = true,  .resourceId = IDC_INPUT_FORMAT_P210 },

    // 4:4:4
    { .name = L"YV24",  .mediaSubtype = MEDIASUBTYPE_YV24,  .avsType = VideoInfo::CS_YV24,      .bitCount = 24, .componentsPerPixel = 1, .subsampleWidthRatio = 1, .subsampleHeightRatio = 1, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_YV24 },

    // RGB
    { .name = L"RGB24", .mediaSubtype = MEDIASUBTYPE_RGB24, .avsType = VideoInfo::CS_BGR24,     .bitCount = 24, .componentsPerPixel = 3, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_RGB24 },
    { .name = L"RGB32", .mediaSubtype = MEDIASUBTYPE_RGB32, .avsType = VideoInfo::CS_BGR32,     .bitCount = 32, .componentsPerPixel = 4, .subsampleWidthRatio = 0, .subsampleHeightRatio = 0, .areUVPlanesInterleaved = false, .resourceId = IDC_INPUT_FORMAT_RGB32 },
    // RGB48 will not work because LAV Filters outputs R-G-B pixel order while AviSynth+ expects B-G-R
};

size_t Format::INPUT_MEDIA_SAMPLE_BUFFER_PADDING;
size_t Format::OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING;

const int Format::_MASK_PERMUTE_V256 = 0b11011000;
size_t Format::_vectorSize;

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

auto Format::Init() -> void {
    if (g_env.IsSupportAVXx()) {
        _vectorSize = sizeof(__m256i);
    } else if (g_env.IsSupportSSSE3()) {
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

auto Format::GetVideoFormat(const AM_MEDIA_TYPE &mediaType) -> VideoFormat {
    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    const REFERENCE_TIME frameDuration = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;

    VideoFormat ret {
        .pixelFormat = LookupMediaSubtype(mediaType.subtype),
        .pixelAspectRatio = PAR_SCALE_FACTOR,
        .hdrType = 0,
        .hdrLuminance = 0,
        .bmi = *GetBitmapInfo(mediaType)
    };
    ret.videoInfo = {
        .width = ret.bmi.biWidth,
        .height = abs(ret.bmi.biHeight),
        .fps_numerator = UNITS,
        .fps_denominator = static_cast<unsigned int>(frameDuration),
        .num_frames = NUM_FRAMES_FOR_INFINITE_STREAM,
        .pixel_type = ret.pixelFormat->avsType,
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

auto Format::WriteSample(const VideoFormat &videoFormat, PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void {
    const std::array srcSlices = { srcFrame->GetReadPtr(), srcFrame->GetReadPtr(PLANAR_U), srcFrame->GetReadPtr(PLANAR_V) };
    const std::array srcStrides = { srcFrame->GetPitch(), srcFrame->GetPitch(PLANAR_U), srcFrame->GetPitch(PLANAR_V) };

    CopyToOutput(videoFormat, srcSlices, srcStrides, dstBuffer, srcFrame->GetRowSize(), srcFrame->GetHeight(), avsEnv);
}

auto Format::CreateFrame(const VideoFormat &videoFormat, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> PVideoFrame {
    PVideoFrame frame = avsEnv->NewVideoFrame(videoFormat.videoInfo, static_cast<int>(_vectorSize));

    const std::array dstSlices = { frame->GetWritePtr(), frame->GetWritePtr(PLANAR_U), frame->GetWritePtr(PLANAR_V) };
    const std::array dstStrides = { frame->GetPitch(), frame->GetPitch(PLANAR_U), frame->GetPitch(PLANAR_V) };

    CopyFromInput(videoFormat, srcBuffer, dstSlices, dstStrides, frame->GetRowSize(), frame->GetHeight(), avsEnv);

    return frame;
}

auto Format::CopyFromInput(const VideoFormat &videoFormat, const BYTE *srcBuffer, const std::array<BYTE *, 3> &dstSlices, const std::array<int, 3> &dstStrides, int rowSize, int height, IScriptEnvironment *avsEnv) -> void {
    // bmi.biWidth should be "set equal to the surface stride in pixels" according to the doc of BITMAPINFOHEADER
    int srcMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.ComponentSize() * videoFormat.pixelFormat->componentsPerPixel;
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

    avsEnv->BitBlt(dstSlices[0], dstStrides[0], srcMainPlane, srcMainPlaneStride, rowSize, height);

    if (videoFormat.pixelFormat->avsType & VideoInfo::CS_INTERLEAVED) {
        return;
    }

    if (const int srcUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        const BYTE *srcUVStart = srcBuffer + srcMainPlaneSize;
        const int srcUVStride = srcMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int srcUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(Deinterleave<0, 0>)* DeinterleaveFunc;
        if (videoFormat.videoInfo.ComponentSize() == 1) {
            if (g_env.IsSupportAVXx()) {
                DeinterleaveFunc = Deinterleave<2, 1>;
            } else if (g_env.IsSupportSSSE3()) {
                DeinterleaveFunc = Deinterleave<1, 1>;
            } else {
                DeinterleaveFunc = Deinterleave<0, 1>;
            }
        } else {
            if (g_env.IsSupportAVXx()) {
                DeinterleaveFunc = Deinterleave<2, 2>;
            } else if (g_env.IsSupportSSSE3()) {
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

        avsEnv->BitBlt(dstSlices[1], dstStrides[1], srcU, srcUVStride, srcUVRowSize, srcUVHeight);
        avsEnv->BitBlt(dstSlices[2], dstStrides[2], srcV, srcUVStride, srcUVRowSize, srcUVHeight);
    }
}

auto Format::CopyToOutput(const VideoFormat &videoFormat, const std::array<const BYTE *, 3> &srcSlices, const std::array<int, 3> &srcStrides, BYTE *dstBuffer, int rowSize, int height, IScriptEnvironment *avsEnv) -> void {
    int dstMainPlaneStride = videoFormat.bmi.biWidth * videoFormat.videoInfo.ComponentSize() * videoFormat.pixelFormat->componentsPerPixel;
    ASSERT(rowSize <= dstMainPlaneStride);
    ASSERT(height >= abs(videoFormat.bmi.biHeight));
    const int dstMainPlaneSize = dstMainPlaneStride * height;
    BYTE *dstMainPlane = dstBuffer;

    // AviSynth+'s convert functions always produce bottom-up DIB, so we invert the DIB if downstream needs top-down
    if (videoFormat.bmi.biCompression == BI_RGB && videoFormat.bmi.biHeight < 0) {
        dstMainPlane += static_cast<size_t>(dstMainPlaneSize) - dstMainPlaneStride;
        dstMainPlaneStride = -dstMainPlaneStride;
    }

    avsEnv->BitBlt(dstMainPlane, dstMainPlaneStride, srcSlices[0], srcStrides[0], rowSize, height);

    if (videoFormat.pixelFormat->avsType & VideoInfo::CS_INTERLEAVED) {
        return;
    }

    if (const int dstUVHeight = height / videoFormat.pixelFormat->subsampleHeightRatio; videoFormat.pixelFormat->areUVPlanesInterleaved) {
        BYTE *dstUVStart = dstBuffer + dstMainPlaneSize;
        const int dstUVStride = dstMainPlaneStride * 2 / videoFormat.pixelFormat->subsampleWidthRatio;
        const int dstUVRowSize = rowSize * 2 / videoFormat.pixelFormat->subsampleWidthRatio;

        decltype(Interleave<0, 0>)* InterleaveFunc;
        if (videoFormat.videoInfo.ComponentSize() == 1) {
            if (g_env.IsSupportAVXx()) {
                InterleaveFunc = Interleave<2, 1>;
            } else {
                InterleaveFunc = Interleave<1, 1>;
            }
        } else {
            if (g_env.IsSupportAVXx()) {
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

        avsEnv->BitBlt(dstU, dstUVStride, srcSlices[1], srcStrides[1], dstUVRowSize, dstUVHeight);
        avsEnv->BitBlt(dstV, dstUVStride, srcSlices[2], srcStrides[2], dstUVRowSize, dstUVHeight);
    }
}

}
