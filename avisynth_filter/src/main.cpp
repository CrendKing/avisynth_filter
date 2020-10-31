#include "pch.h"
#include "avs_handler.h"
#include "constants.h"
#include "environment.h"
#include "filter.h"
#include "format.h"
#include "prop_settings.h"
#include "prop_status.h"


#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif
#pragma comment(lib, "winmm.lib")

namespace AvsFilter {

ReferenceCountPointer<Environment> g_env;
ReferenceCountPointer<AvsHandler> g_avs;

static REGFILTERPINS REG_PINS[] = {
    { nullptr                                // pin name (obsolete)
    , FALSE                                  // is pin rendered?
    , FALSE                                  // is this output pin?
    , FALSE                                  // Can the filter create zero instances?
    , FALSE                                  // Does the filter create multiple instances?
    , &CLSID_NULL                            // filter CLSID the pin connects to (obsolete)
    , nullptr                                // pin name the pin connects to (obsolete)
    , 0                                      // pin media type count (to be filled in InitRoutine())
    , nullptr },                             // pin media types (to be filled in InitRoutine())

    { nullptr                                // pin name (obsolete)
    , FALSE                                  // is pin rendered?
    , TRUE                                   // is this output pin?
    , FALSE                                  // Can the filter create zero instances?
    , FALSE                                  // Does the filter create multiple instances?
    , &CLSID_NULL                            // filter CLSID the pin connects to (obsolete)
    , nullptr                                // pin name the pin connects to (obsolete)
    , 0                                      // pin media type count (to be filled in InitRoutine())
    , nullptr },                             // pin media types (to be filled in InitRoutine())
};

static constexpr AMOVIESETUP_FILTER REG_FILTER = {
    &AvsFilter::CLSID_AviSynthFilter,        // filter CLSID
    FILTER_NAME_WIDE,                        // filter name
    MERIT_DO_NOT_USE + 1,                    // filter merit
    sizeof(REG_PINS) / sizeof(REG_PINS[0]),  // pin count
    REG_PINS                                 // pin information
};

template <typename T>
static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    if constexpr (std::is_same_v<T, CAviSynthFilter>) {
        if (!g_env) {
            g_env = new Environment();
        } else {
            g_env.AddRef();
        }

        if (!g_avs) {
            g_avs = new AvsHandler();
        } else {
            g_avs.AddRef();
        }
    }

    CUnknown *newInstance = new T(pUnk, phr);
    if (newInstance == nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newInstance;
}

static auto RegisterFilter() -> HRESULT {
    std::vector<REGPINTYPES> pinTypes;
    for (const Format::Definition &info : Format::DEFINITIONS) {
        pinTypes.emplace_back(REGPINTYPES { &MEDIATYPE_Video, &info.mediaSubtype });
    }

    REG_PINS[0].lpMediaType = pinTypes.data();
    REG_PINS[0].nMediaTypes = static_cast<UINT>(pinTypes.size());
    REG_PINS[1].lpMediaType = REG_PINS[0].lpMediaType;
    REG_PINS[1].nMediaTypes = REG_PINS[0].nMediaTypes;

    return AMovieDllRegisterServer2(TRUE);
}

}

CFactoryTemplate g_Templates[] = {
    { FILTER_NAME_WIDE
    , &AvsFilter::CLSID_AviSynthFilter
    , AvsFilter::CreateInstance<AvsFilter::CAviSynthFilter>
    , nullptr
    , &AvsFilter::REG_FILTER
    },

    { SETTINGS_WIDE
    , &AvsFilter::CLSID_AvsPropSettings
    , AvsFilter::CreateInstance<AvsFilter::CAvsFilterPropSettings>
    },

    { STATUS_WIDE
    , &AvsFilter::CLSID_AvsPropStatus
    , AvsFilter::CreateInstance<AvsFilter::CAvsFilterPropStatus>
    },
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

auto STDAPICALLTYPE DllRegisterServer() -> HRESULT {
    return AvsFilter::RegisterFilter();
}

auto STDAPICALLTYPE DllUnregisterServer() -> HRESULT {
    return AMovieDllRegisterServer2(FALSE);
}

extern "C" DECLSPEC_NOINLINE BOOL WINAPI DllEntryPoint(HINSTANCE hInstance, ULONG ulReason, __inout_opt LPVOID pv);

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    return DllEntryPoint(hModule, ul_reason_for_call, lpReserved);
}
