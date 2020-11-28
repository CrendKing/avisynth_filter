// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "format.h"
#include "api.h"
#include "constants.h"


namespace AvsFilter {

static const __m128i DEINTERLEAVE_MASK_8_BIT_1 = _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 14, 12, 10, 8, 6, 4, 2, 0);
static const __m128i DEINTERLEAVE_MASK_8_BIT_2 = _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 15, 13, 11, 9, 7, 5, 3, 1);
static const __m128i DEINTERLEAVE_MASK_16_BIT_1 = _mm_set_epi8(29, 28, 25, 24, 21, 20, 17, 16, 13, 12, 9, 8, 5, 4, 1, 0);
static const __m128i DEINTERLEAVE_MASK_16_BIT_2 = _mm_set_epi8(31, 30, 27, 26, 23, 22, 19, 18, 15, 14, 11, 10, 7, 6, 3, 2);

const std::vector<Format::Definition> Format::DEFINITIONS = {
    /* 0 */  { MEDIASUBTYPE_NV12, VideoInfo::CS_YV12, 12, 1 },
    /* 1 */  { MEDIASUBTYPE_YV12, VideoInfo::CS_YV12, 12, 1 },
    /* 2 */  { MEDIASUBTYPE_I420, VideoInfo::CS_YV12, 12, 1 },
    /* 3 */  { MEDIASUBTYPE_IYUV, VideoInfo::CS_YV12, 12, 1 },

    // P010 has the most significant 6 bits zero-padded, while AviSynth expects the least significant bits padded
    // P010 without right shifting 6 bits on every WORD is equivalent to P016, without precision loss
    /* 4 */  { MEDIASUBTYPE_P010, VideoInfo::CS_YUV420P16, 24, 1 },

    /* 5 */  { MEDIASUBTYPE_P016, VideoInfo::CS_YUV420P16, 24, 1 },

    // packed formats such as YUY2 are twice as wide as unpacked formats per pixel
    /* 6 */  { MEDIASUBTYPE_YUY2, VideoInfo::CS_YUY2, 16, 2 },
    /* 7 */  { MEDIASUBTYPE_UYVY, VideoInfo::CS_YUY2, 16, 2 },

    /* 8 */  { MEDIASUBTYPE_RGB24, VideoInfo::CS_BGR24, 24, 3 },
    /* 9 */  { MEDIASUBTYPE_RGB32, VideoInfo::CS_BGR32, 24, 4 },
    /* 10 */ { MEDIASUBTYPE_RGB48, VideoInfo::CS_BGR48, 48, 3 },
};

auto Format::VideoFormat::operator!=(const VideoFormat &other) const -> bool {
    return definition != other.definition
        || memcmp(&videoInfo, &other.videoInfo, sizeof(videoInfo)) != 0
        || pixelAspectRatio != other.pixelAspectRatio
        || hdrType != other.hdrType
        || hdrLuminance != other.hdrLuminance
        || bmi.biSize != other.bmi.biSize
        || memcmp(&bmi, &other.bmi, bmi.biSize) != 0;
}

auto Format::VideoFormat::GetCodecFourCC() const -> DWORD {
    return FOURCCMap(&DEFINITIONS[definition].mediaSubtype).GetFOURCC();
}

auto Format::VideoFormat::GetCodecName() const -> std::string {
    const CLSID subtype = DEFINITIONS[definition].mediaSubtype;

    if (bmi.biCompression == BI_RGB) {
        if (subtype == MEDIASUBTYPE_RGB24) {
            return "RGB24";
        } else if (subtype == MEDIASUBTYPE_RGB32) {
            return "RGB32";
        } else if (subtype == MEDIASUBTYPE_RGB48) {
            return "RGB48";
        } else {
            return "RGB0";
        }
    } else {
        const DWORD fourCC = GetCodecFourCC();
        return std::string(reinterpret_cast<const char *>(&fourCC), 4);
    }
}

auto Format::LookupMediaSubtype(const CLSID &mediaSubtype) -> std::optional<int> {
    for (int i = 0; i < static_cast<int>(DEFINITIONS.size()); ++i) {
        if (mediaSubtype == DEFINITIONS[i].mediaSubtype) {
            return i;
        }
    }

    return std::nullopt;
}

auto Format::LookupAvsType(int avsType) -> std::vector<int> {
    std::vector<int> indices;

    for (int i = 0; i < static_cast<int>(DEFINITIONS.size()); ++i) {
        if (avsType == DEFINITIONS[i].avsType) {
            indices.emplace_back(i);
        }
    }

    return indices;
}

auto Format::GetVideoFormat(const AM_MEDIA_TYPE &mediaType) -> VideoFormat {
    VideoFormat info {};

    const VIDEOINFOHEADER *vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    const REFERENCE_TIME frameTime = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;

    info.definition = *LookupMediaSubtype(mediaType.subtype);
    info.bmi = *GetBitmapInfo(mediaType);

    info.videoInfo.width = info.bmi.biWidth;
    info.videoInfo.height = abs(info.bmi.biHeight);
    info.videoInfo.fps_numerator = UNITS;
    info.videoInfo.fps_denominator = static_cast<unsigned int>(frameTime);
    info.videoInfo.pixel_type = DEFINITIONS[info.definition].avsType;
    info.videoInfo.num_frames = NUM_FRAMES_FOR_INFINITE_STREAM;

    info.pixelAspectRatio = PAR_SCALE_FACTOR;
    if (SUCCEEDED(CheckVideoInfo2Type(&mediaType))) {
        const VIDEOINFOHEADER2* vih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType.pbFormat);
        if (vih2->dwPictAspectRatioY > 0) {
            /*
             * pixel aspect ratio = display aspect ratio (DAR) / storage aspect ratio (SAR)
             * DAR comes from VIDEOINFOHEADER2.dwPictAspectRatioX / VIDEOINFOHEADER2.dwPictAspectRatioY
             * SAR comes from info.videoInfo.width / info.videoInfo.height
             */
            info.pixelAspectRatio = static_cast<int>(llMulDiv(static_cast<LONGLONG>(vih2->dwPictAspectRatioX) * info.videoInfo.height,
                                                     PAR_SCALE_FACTOR,
                                                     static_cast<LONGLONG>(vih2->dwPictAspectRatioY) * info.videoInfo.width,
                                                     0));
        }
    }

    info.hdrType = 0;
    info.hdrLuminance = 0;

    return info;
}

auto Format::WriteSample(const VideoFormat &format, PVideoFrame srcFrame, BYTE *dstBuffer, IScriptEnvironment *avsEnv) -> void {
    const BYTE *srcSlices[] = { srcFrame->GetReadPtr(), srcFrame->GetReadPtr(PLANAR_U), srcFrame->GetReadPtr(PLANAR_V) };
    const int srcStrides[] = { srcFrame->GetPitch(), srcFrame->GetPitch(PLANAR_U), srcFrame->GetPitch(PLANAR_V) };

    CopyToOutput(format, srcSlices, srcStrides, dstBuffer, srcFrame->GetRowSize(), srcFrame->GetHeight(), avsEnv);
}

auto Format::CreateFrame(const VideoFormat &format, const BYTE *srcBuffer, IScriptEnvironment *avsEnv) -> PVideoFrame {
    PVideoFrame frame = avsEnv->NewVideoFrame(format.videoInfo, sizeof(__m128i));

    BYTE *dstSlices[] = { frame->GetWritePtr(), frame->GetWritePtr(PLANAR_U), frame->GetWritePtr(PLANAR_V) };
    const int dstStrides[] = { frame->GetPitch(), frame->GetPitch(PLANAR_U), frame->GetPitch(PLANAR_V) };

    CopyFromInput(format, srcBuffer, dstSlices, dstStrides, frame->GetRowSize(), frame->GetHeight(), avsEnv);

    return frame;
}

auto Format::CopyFromInput(const VideoFormat &format, const BYTE *srcBuffer, BYTE *dstSlices[], const int dstStrides[], int dstRowSize, int dstHeight, IScriptEnvironment *avsEnv) -> void {
    const Definition &def = DEFINITIONS[format.definition];

    const int srcStride = format.bmi.biWidth * format.videoInfo.ComponentSize() * def.componentsPerPixel;
    const int rowSize = min(srcStride, dstRowSize);
    const int height = min(abs(format.bmi.biHeight), dstHeight);
    const int srcDefaultPlaneSize = srcStride * height;

    const BYTE *srcDefaultPlane;
    int srcDefaultPlaneStride;
    if ((def.avsType & VideoInfo::CS_BGR) != 0 && format.bmi.biCompression == BI_RGB && format.bmi.biHeight < 0) {
        // positive height for RGB definition is bottom-up DIB, negative is top-down
        // AviSynth is always bottom-up
        srcDefaultPlane = srcBuffer + srcDefaultPlaneSize - srcStride;
        srcDefaultPlaneStride = -srcStride;
    } else {
        srcDefaultPlane = srcBuffer;
        srcDefaultPlaneStride = srcStride;
    }

    avsEnv->BitBlt(dstSlices[0], dstStrides[0], srcDefaultPlane, srcDefaultPlaneStride, rowSize, height);

    if ((def.avsType & VideoInfo::CS_INTERLEAVED) == 0) {
        // 4:2:0 unpacked formats

        if (def.mediaSubtype == MEDIASUBTYPE_IYUV || def.mediaSubtype == MEDIASUBTYPE_I420 || def.mediaSubtype == MEDIASUBTYPE_YV12) {
            // these formats' U and V planes are not interleaved. use BitBlt to efficiently copy

            const BYTE *srcPlane1 = srcBuffer + srcDefaultPlaneSize;
            const BYTE *srcPlane2 = srcPlane1 + srcDefaultPlaneSize / 4;

            const BYTE *srcU;
            const BYTE *srcV;
            if (def.mediaSubtype == MEDIASUBTYPE_YV12) {
                // YV12 has V plane first

                srcU = srcPlane2;
                srcV = srcPlane1;
            } else {
                srcU = srcPlane1;
                srcV = srcPlane2;
            }

            avsEnv->BitBlt(dstSlices[1], dstStrides[1], srcU, srcStride / 2, rowSize / 2, height / 2);
            avsEnv->BitBlt(dstSlices[2], dstStrides[2], srcV, srcStride / 2, rowSize / 2, height / 2);
        } else {
            // interleaved U and V planes. copy byte by byte
            // consider using intrinsics for better performance

            const BYTE *srcUVStart = srcBuffer + srcDefaultPlaneSize;
            __m128i mask1, mask2;

            if (format.videoInfo.ComponentSize() == 1) {
                mask1 = DEINTERLEAVE_MASK_8_BIT_1;
                mask2 = DEINTERLEAVE_MASK_8_BIT_2;
            } else {
                mask1 = DEINTERLEAVE_MASK_16_BIT_1;
                mask2 = DEINTERLEAVE_MASK_16_BIT_2;
            }

            if (format.videoInfo.ComponentSize() == 1) {
                Deinterleave<uint8_t>(srcUVStart, srcStride, dstSlices[1], dstSlices[2], dstStrides[1], rowSize, height / 2, mask1, mask2);
            } else {
                Deinterleave<uint16_t>(srcUVStart, srcStride, dstSlices[1], dstSlices[2], dstStrides[1], rowSize, height / 2, mask1, mask2);
            }
        }
    }
}

auto Format::CopyToOutput(const VideoFormat &format, const BYTE *srcSlices[], const int srcStrides[], BYTE *dstBuffer, int srcRowSize, int srcHeight, IScriptEnvironment *avsEnv) -> void {
    const Definition &def = DEFINITIONS[format.definition];

    const int dstStride = format.bmi.biWidth * format.videoInfo.ComponentSize() * def.componentsPerPixel;
    const int rowSize = min(dstStride, srcRowSize);
    const int height = min(abs(format.bmi.biHeight), srcHeight);
    const int dstDefaultPlaneSize = dstStride * height;

    BYTE *dstDefaultPlane;
    int dstDefaultPlaneStride;
    if ((def.avsType & VideoInfo::CS_BGR) != 0 && format.bmi.biCompression == BI_RGB && format.bmi.biHeight < 0) {
        dstDefaultPlane = dstBuffer + dstDefaultPlaneSize - dstStride;
        dstDefaultPlaneStride = -dstStride;
    } else {
        dstDefaultPlane = dstBuffer;
        dstDefaultPlaneStride = dstStride;
    }

    avsEnv->BitBlt(dstDefaultPlane, dstDefaultPlaneStride, srcSlices[0], srcStrides[0], rowSize, height);

    if ((def.avsType & VideoInfo::CS_INTERLEAVED) == 0) {
        if (def.mediaSubtype == MEDIASUBTYPE_IYUV || def.mediaSubtype == MEDIASUBTYPE_I420 || def.mediaSubtype == MEDIASUBTYPE_YV12) {
            BYTE *dstPlane1 = dstBuffer + dstDefaultPlaneSize;
            BYTE *dstPlane2 = dstPlane1 + dstDefaultPlaneSize / 4;

            BYTE *dstU;
            BYTE *dstV;
            if (def.mediaSubtype == MEDIASUBTYPE_YV12) {
                dstU = dstPlane2;
                dstV = dstPlane1;
            } else {
                dstU = dstPlane1;
                dstV = dstPlane2;
            }

            avsEnv->BitBlt(dstU, dstStride / 2, srcSlices[1], srcStrides[1], rowSize / 2, height / 2);
            avsEnv->BitBlt(dstV, dstStride / 2, srcSlices[2], srcStrides[2], rowSize / 2, height / 2);
        } else {
            BYTE *dstUVStart = dstBuffer + dstDefaultPlaneSize;

            if (format.videoInfo.ComponentSize() == 1) {
                Interleave<uint8_t>(srcSlices[1], srcSlices[2], srcStrides[1], dstUVStart, dstStride, rowSize / 2, height / 2);
            } else {
                Interleave<uint16_t>(srcSlices[1], srcSlices[2], srcStrides[1], dstUVStart, dstStride, rowSize / 2, height / 2);
            }
        }
    }
}

}
