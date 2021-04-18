// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "avs_handler.h"
#include "constants.h"
#include "environment.h"
#include "filter.h"
#include "format.h"
#include "prop_settings.h"
#include "prop_status.h"


#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "winmm.lib")

namespace AvsFilter {

Environment g_env;
ReferenceCountPointer<AvsHandler> g_avs;

static REGFILTERPINS REG_PINS[] = {
    { .strName = nullptr                              // pin name (obsolete)
    , .bRendered = FALSE                              // is pin rendered?
    , .bOutput = FALSE                                // is this output pin?
    , .bZero = FALSE                                  // Can the filter create zero instances?
    , .bMany = FALSE                                  // Does the filter create multiple instances?
    , .clsConnectsToFilter = &CLSID_NULL              // filter CLSID the pin connects to (obsolete)
    , .strConnectsToPin = nullptr                     // pin name the pin connects to (obsolete)
    , .nMediaTypes = 0                                // pin media type count (to be filled in RegisterFilter())
    , .lpMediaType = nullptr                          // pin media types (to be filled in RegisterFilter())
    },

    { .strName = nullptr
    , .bRendered = FALSE
    , .bOutput = TRUE
    , .bZero = FALSE
    , .bMany = FALSE
    , .clsConnectsToFilter = &CLSID_NULL
    , .strConnectsToPin = nullptr
    , .nMediaTypes = 0
    , .lpMediaType = nullptr
    },
};

static constexpr AMOVIESETUP_FILTER REG_FILTER = {
    .clsID = &CLSID_AviSynthFilter,                   // filter CLSID
    .strName = FILTER_NAME_FULL,                      // filter name
    .dwMerit = MERIT_DO_NOT_USE + 1,                  // filter merit
    .nPins = sizeof(REG_PINS) / sizeof(REG_PINS[0]),  // pin count
    .lpPin = REG_PINS                                 // pin information
};

template <typename T>
static constexpr auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    if constexpr (std::is_same_v<T, CAviSynthFilter>) {
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
    for (const Format::PixelFormat &pixelFormat : Format::PIXEL_FORMATS) {
        pinTypes.emplace_back(&MEDIATYPE_Video, &pixelFormat.mediaSubtype);
    }

    REG_PINS[0].lpMediaType = pinTypes.data();
    REG_PINS[0].nMediaTypes = static_cast<UINT>(pinTypes.size());
    REG_PINS[1].lpMediaType = REG_PINS[0].lpMediaType;
    REG_PINS[1].nMediaTypes = REG_PINS[0].nMediaTypes;

    const HRESULT hr = AMovieDllRegisterServer2(TRUE);
    if (FAILED(hr)) {
        g_env.Log(L"Registeration failed: %li", hr);
    }

    return hr;
}

}

CFactoryTemplate g_Templates[] = {
    { .m_Name = FILTER_NAME_FULL
    , .m_ClsID = &AvsFilter::CLSID_AviSynthFilter
    , .m_lpfnNew = AvsFilter::CreateInstance<AvsFilter::CAviSynthFilter>
    , .m_lpfnInit = nullptr
    , .m_pAMovieSetup_Filter = &AvsFilter::REG_FILTER
    },

    { .m_Name = SETTINGS_NAME_FULL
    , .m_ClsID = &AvsFilter::CLSID_AvsPropSettings
    , .m_lpfnNew = AvsFilter::CreateInstance<AvsFilter::CAvsFilterPropSettings>
    },

    { .m_Name = STATUS_NAME_FULL
    , .m_ClsID = &AvsFilter::CLSID_AvsPropStatus
    , .m_lpfnNew = AvsFilter::CreateInstance<AvsFilter::CAvsFilterPropStatus>
    },
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

auto STDAPICALLTYPE DllRegisterServer() -> HRESULT {
    return AvsFilter::RegisterFilter();
}

auto STDAPICALLTYPE DllUnregisterServer() -> HRESULT {
    return AMovieDllRegisterServer2(FALSE);
}

extern "C" DECLSPEC_NOINLINE auto WINAPI DllEntryPoint(HINSTANCE hInstance, ULONG ulReason, __inout_opt LPVOID pv) -> BOOL;

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    return DllEntryPoint(hModule, ul_reason_for_call, lpReserved);
}
