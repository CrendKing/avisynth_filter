// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace SynthFilter {

#define CheckHr(expr)     \
    {                     \
        hr = (expr);      \
        if (FAILED(hr)) { \
            return hr;    \
        }                 \
    }
}

// from FFmpeg
#define FFALIGN(x, alignment) (((x) + (alignment) -1) & ~((alignment) -1))