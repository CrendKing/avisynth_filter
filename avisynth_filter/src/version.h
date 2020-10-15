#pragma once


namespace AvsFilter {

#define STR_HELPER(x)                   #x
#define STR(x)                          STR_HELPER(x)

#define FILTER_NAME_BASE                "AviSynth Filter"
#define FILTER_VERSION_MAJOR            0
#define FILTER_VERSION_MINOR            7
#define FILTER_VERSION_PATCH            1
#define FILTER_VERSION_STRING           STR(FILTER_VERSION_MAJOR) "." STR(FILTER_VERSION_MINOR) "." STR(FILTER_VERSION_PATCH)

}
