// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE.txt

#pragma once


namespace SynthFilter {

#ifdef _DEBUG
    #define FILTER_VARIANT        " [Debug]"
#else
    #define FILTER_VARIANT
#endif

#ifdef FILTER_GIT_HASH
    #define FILTER_VERSION_DETAIL " # " FILTER_GIT_HASH
#else
    #define FILTER_VERSION_DETAIL
#endif

#define STRINGIZE_HELPER(x)       #x
#define STRINGIZE(x)              STRINGIZE_HELPER(x)
#define WIDEN_HELPER(s)           L##s
#define WIDEN(s)                  WIDEN_HELPER(s)

#ifdef AVSF_AVISYNTH
    #define FILTER_NAME_BASE      "AviSynth Filter"
    #define FILTER_FILENAME_BASE  "avisynth_filter"
#else
    #define FILTER_NAME_BASE      "VapourSynth Filter"
    #define FILTER_FILENAME_BASE  "vapoursynth_filter"
#endif

#define FILTER_NAME_WIDE          WIDEN(FILTER_NAME_BASE)
#define FILTER_VERSION_MAJOR      1
#define FILTER_VERSION_MINOR      4
#define FILTER_VERSION_PATCH      10
#define FILTER_VERSION_SHORT      STRINGIZE(FILTER_VERSION_MAJOR) "." STRINGIZE(FILTER_VERSION_MINOR) "." STRINGIZE(FILTER_VERSION_PATCH)
#define FILTER_VERSION_LONG       FILTER_VERSION_SHORT FILTER_VARIANT FILTER_VERSION_DETAIL

}
