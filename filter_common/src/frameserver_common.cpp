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

        // generate new DAR if the new SAR differs from the old one
        // because AviSynth does not tell us anything about DAR, scaled the DAR wit the ratio between new SAR and old SAR
        if (_scriptVideoInfo.width * abs(newBmi->biHeight) != _scriptVideoInfo.height * newBmi->biWidth) {
            const long long ax = static_cast<long long>(newVih2->dwPictAspectRatioX) * _scriptVideoInfo.width * std::abs(newBmi->biHeight);
            const long long ay = static_cast<long long>(newVih2->dwPictAspectRatioY) * _scriptVideoInfo.height * newBmi->biWidth;
            const long long gcd = std::gcd(ax, ay);
            newVih2->dwPictAspectRatioX = static_cast<DWORD>(ax / gcd);
            newVih2->dwPictAspectRatioY = static_cast<DWORD>(ay / gcd);
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
