// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace AvsFilter {

#define STRINGIZE_HELPER(x)             #x
#define STRINGIZE(x)                    STRINGIZE_HELPER(x)
#define WIDEN_HELPER(s)                 L ## s
#define WIDEN(s)                        WIDEN_HELPER(s)

#define FILTER_NAME_BASE                "AviSynth Filter"
#define FILTER_NAME_WIDE                WIDEN(FILTER_NAME_BASE)
#define FILTER_FILENAME_BASE            "avisynth_filter"
#define FILTER_VERSION_MAJOR            0
#define FILTER_VERSION_MINOR            9
#define FILTER_VERSION_PATCH            4
#define FILTER_VERSION_STRING           STRINGIZE(FILTER_VERSION_MAJOR) "." STRINGIZE(FILTER_VERSION_MINOR) "." STRINGIZE(FILTER_VERSION_PATCH)

}
