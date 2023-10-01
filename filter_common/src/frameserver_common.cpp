// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "frameserver.h"


namespace SynthFilter {

auto FrameServerCommon::SetScriptPath(const std::filesystem::path &scriptPath) -> void {
    _scriptPath = scriptPath;
}

auto MainFrameServer::GetErrorString() const -> std::optional<std::string> {
    return _errorString.empty() ? std::nullopt : std::make_optional(_errorString);
}

/**
 * Create media type based on a template while changing its subtype. Also change fields in format if necessary.
 *
 * For example, when the original subtype has 8-bit samples and new subtype has 16-bit,
 * all "size" and FourCC values will be adjusted.
 */
auto AuxFrameServer::GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType {
    FOURCCMap fourCC(&pixelFormat.mediaSubtype);

    CMediaType newMediaType(*templateMediaType);
    newMediaType.SetSubtype(&pixelFormat.mediaSubtype);

    VIDEOINFOHEADER *newVih = reinterpret_cast<VIDEOINFOHEADER *>(newMediaType.Format());
    BITMAPINFOHEADER *newBmi;

    if (SUCCEEDED(CheckVideoInfo2Type(&newMediaType))) {
        VIDEOINFOHEADER2 *newVih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(newMediaType.Format());
        newBmi = &newVih2->bmiHeader;

        // if the script changes the video dimension, we need to adjust the DAR
        // assuming the pixel aspect ratio remains the same, new DAR = PAR / new (script) SAR
        const auto &sourceVideoInfo = FrameServerCommon::GetInstance()._sourceVideoInfo;
        if (_scriptVideoInfo.width != sourceVideoInfo.width || _scriptVideoInfo.height != sourceVideoInfo.height) {
            unsigned long long darX = static_cast<unsigned long long>(newVih2->dwPictAspectRatioX) * sourceVideoInfo.height * _scriptVideoInfo.width;
            unsigned long long darY = static_cast<unsigned long long>(newVih2->dwPictAspectRatioY) * sourceVideoInfo.width * _scriptVideoInfo.height;
            CoprimeIntegers(darX, darY);
            newVih2->dwPictAspectRatioX = static_cast<DWORD>(darX);
            newVih2->dwPictAspectRatioY = static_cast<DWORD>(darY);
        }
    } else {
        newBmi = &newVih->bmiHeader;
    }

    newVih->rcSource = { .left = 0, .top = 0, .right = _scriptVideoInfo.width, .bottom = _scriptVideoInfo.height };
    newVih->rcTarget = newVih->rcSource;
#ifdef AVSF_AVISYNTH
    newVih->AvgTimePerFrame = llMulDiv(_scriptVideoInfo.fps_denominator, UNITS, _scriptVideoInfo.fps_numerator, 0);
#else
    newVih->AvgTimePerFrame = llMulDiv(_scriptVideoInfo.fpsDen, UNITS, _scriptVideoInfo.fpsNum, 0);
#endif
    newBmi->biWidth = _scriptVideoInfo.width;
    newBmi->biHeight = _scriptVideoInfo.height;
    newBmi->biBitCount = pixelFormat.bitCount;
    newBmi->biSizeImage = GetBitmapSize(newBmi);
    newMediaType.SetSampleSize(newBmi->biSizeImage);

    if (fourCC == pixelFormat.mediaSubtype) {
        // uncompressed formats (such as RGB32) have different GUIDs
        newBmi->biCompression = fourCC.GetFOURCC();
    } else {
        newBmi->biCompression = BI_RGB;
    }

    return newMediaType;
}

}
