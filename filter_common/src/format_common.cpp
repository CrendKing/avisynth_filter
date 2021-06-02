// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "format.h"
#include "environment.h"


namespace SynthFilter {

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
