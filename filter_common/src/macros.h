// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace SynthFilter {

#define DISABLE_COPYING(T) \
    T(const T &) = delete; \
    T &operator=(const T &) = delete;

#define CTOR_WITHOUT_COPYING(T) \
    T() = default;              \
    DISABLE_COPYING(T)

// from FFmpeg
#define FFALIGN(x, alignment) (((x) + (alignment) -1) & ~((alignment) -1))

#define CheckHr(expr)     \
    {                     \
        hr = (expr);      \
        if (FAILED(hr)) { \
            return hr;    \
        }                 \
    }
}