#include "pch.h"
#include "format.h"
#include "constants.h"


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
        || bmi.biSize != other.bmi.biSize
        || memcmp(&bmi, &other.bmi, bmi.biSize) != 0
        || memcmp(vih, other.vih, sizeof(VIDEOINFOHEADER)) != 0;
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
        const DWORD fourCC = FOURCCMap(&subtype).GetFOURCC();
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

    info.vih = reinterpret_cast<VIDEOINFOHEADER *>(mediaType.pbFormat);
    const REFERENCE_TIME frameTime = info.vih->AvgTimePerFrame > 0 ? info.vih->AvgTimePerFrame : DEFAULT_AVG_TIME_PER_FRAME;

    info.definition = *LookupMediaSubtype(mediaType.subtype);
    info.bmi = *GetBitmapInfo(mediaType);

    info.videoInfo.width = info.bmi.biWidth;
    info.videoInfo.height = abs(info.bmi.biHeight);
    info.videoInfo.fps_numerator = UNITS;
    info.videoInfo.fps_denominator = static_cast<unsigned int>(frameTime);
    info.videoInfo.pixel_type = DEFINITIONS[info.definition].avsType;
    info.videoInfo.num_frames = NUM_FRAMES_FOR_INFINITE_STREAM;

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

            Deinterleave(srcUVStart, srcStride, dstSlices[1], dstSlices[2], dstStrides[1],
                         rowSize, height / 2, mask1, mask2);
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

            Interleave(srcSlices[1], srcSlices[2], srcStrides[1],
                       dstUVStart, dstStride, rowSize / 2, height / 2, format.videoInfo.ComponentSize());
        }
    }
}

auto Format::Deinterleave(const BYTE *src, int srcStride, BYTE *dst1, BYTE *dst2, int dstStride, int rowSize, int height, __m128i mask1, __m128i mask2) -> void {
    const int iterations = rowSize / sizeof(__m128i);
    const int remainderStart = iterations * sizeof(__m128i);

    for (int y = 0; y < height; ++y) {
        const __m128i *src_128 = reinterpret_cast<const __m128i *>(src);
        __int64 *dst1_64 = reinterpret_cast<__int64 *>(dst1);
        __int64 *dst2_64 = reinterpret_cast<__int64 *>(dst2);

        for (int i = 0; i < iterations; ++i) {
            const __m128i n = *src_128++;
            _mm_storeu_si64(dst1_64++, _mm_shuffle_epi8(n, mask1));
            _mm_storeu_si64(dst2_64++, _mm_shuffle_epi8(n, mask2));
        }

        // copy remaining unaligned bytes (rowSize % sizeof(__m128i) != 0)
        for (int i = remainderStart; i < rowSize; i += 2) {
            dst1[i / 2] = src[i + 0];
            dst2[i / 2] = src[i + 1];
        }

        src += srcStride;
        dst1 += dstStride;
        dst2 += dstStride;
    }
}

auto Format::Interleave(const BYTE *src1, const BYTE *src2, int srcStride, BYTE *dst, int dstStride, int rowSize, int height, uint8_t bytesPerComponent) -> void {
    const int iterations = rowSize / sizeof(__m128i);
    const int remainderStart = iterations * sizeof(__m128i);

    for (int y = 0; y < height; ++y) {
        const __m128i *src1_128 = reinterpret_cast<const __m128i *>(src1);
        const __m128i *src2_128 = reinterpret_cast<const __m128i *>(src2);
        __m128i *dst_128 = reinterpret_cast<__m128i *>(dst);

        for (int i = 0; i < iterations; ++i) {
            const __m128i s1 = *src1_128++;
            const __m128i s2 = *src2_128++;

            if (bytesPerComponent == 1) {
                *dst_128++ = _mm_unpacklo_epi8(s1, s2);
                *dst_128++ = _mm_unpackhi_epi8(s1, s2);
            } else {
                *dst_128++ = _mm_unpacklo_epi16(s1, s2);
                *dst_128++ = _mm_unpackhi_epi16(s1, s2);
            }
        }

        for (int i = remainderStart; i < rowSize; ++i) {
            if (bytesPerComponent == 1) {
                *reinterpret_cast<uint8_t *>(dst + static_cast<size_t>(i) * 2 + 0) = *reinterpret_cast<const uint8_t *>(src1 + i);
                *reinterpret_cast<uint8_t *>(dst + static_cast<size_t>(i) * 2 + sizeof(uint8_t)) = *reinterpret_cast<const uint8_t *>(src2 + i);
            } else {
                *reinterpret_cast<uint16_t *>(dst + static_cast<size_t>(i) * 2 + 0) = *reinterpret_cast<const uint16_t *>(src1 + i);
                *reinterpret_cast<uint16_t *>(dst + static_cast<size_t>(i) * 2 + sizeof(uint16_t)) = *reinterpret_cast<const uint16_t *>(src2 + i);
            }
        }

        src1 += srcStride;
        src2 += srcStride;
        dst += dstStride;
    }
}