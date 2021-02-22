// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace AvsFilter {

#define StrHelper(x)                    #x
#define Str(x)                          StrHelper(x)

#define FILTER_NAME_BASE                "AviSynth Filter"
#define FILTER_FILENAME_BASE            "avisynth_filter"
#define FILTER_VERSION_MAJOR            0
#define FILTER_VERSION_MINOR            9
#define FILTER_VERSION_PATCH            2
#define FILTER_VERSION_STRING           Str(FILTER_VERSION_MAJOR) "." Str(FILTER_VERSION_MINOR) "." Str(FILTER_VERSION_PATCH)

}
