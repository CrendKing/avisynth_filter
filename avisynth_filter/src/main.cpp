#include "pch.h"

#include "constants.h"
#include "filter.h"
#include "format.h"
#include "environment.h"
#include "prop_settings.h"
#include "prop_status.h"


#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif
#pragma comment(lib, "winmm.lib")

const AVS_Linkage *AVS_linkage = nullptr;

namespace AvsFilter {

Environment g_env;

template <typename T>
static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    if (std::is_same_v<T, CAviSynthFilter> && !g_env.Initialize(phr)) {
        return nullptr;
    }

    CUnknown *newInstance = new T(pUnk, phr);
    if (newInstance == nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newInstance;
}

static auto RegisterFilter() -> HRESULT {
    HRESULT hr;

    IFilterMapper2 *filterMapper;
    hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, reinterpret_cast<void **>(&filterMapper));
    if (FAILED(hr)) {
        return hr;
    }

    std::vector<REGPINTYPES> pinTypes;
    for (const Format::Definition &info : Format::DEFINITIONS) {
        pinTypes.emplace_back(REGPINTYPES { &MEDIATYPE_Video, &info.mediaSubtype });
    }

    const REGFILTERPINS2 regPins[2] = {
        { 0                                   // no flag for input pin
        , 1                                   // number of instance
        , static_cast<UINT>(pinTypes.size())  // number of media types
        , pinTypes.data() }                   // media types
    ,
        { REG_PINFLAG_B_OUTPUT                // output pin
        , 1                                   // number of instance
        , static_cast<UINT>(pinTypes.size())  // number of media types
        , pinTypes.data() }                   // media types
    };

    REGFILTER2 regFilter = {
        2,                                    // format version
        MERIT_DO_NOT_USE + 1,                 // filter merit
        2,                                    // pin count
    };
    regFilter.rgPins2 = regPins;

    hr = filterMapper->RegisterFilter(
        CLSID_AviSynthFilter,                 // filter CLSID
        FILTER_NAME_WIDE,                     // filter name
        nullptr,                              // device moniker
        &CLSID_LegacyAmFilterCategory,        // filter category
        FILTER_NAME_WIDE,                     // instance data
        &regFilter                            // filter information
    );

    filterMapper->Release();

    return hr;
}

static auto UnregisterFilter() -> HRESULT {
    HRESULT hr;

    IFilterMapper2 *filterMapper;
    hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, reinterpret_cast<void **>(&filterMapper));
    if (FAILED(hr)) {
        return hr;
    }

    hr = filterMapper->UnregisterFilter(&CLSID_LegacyAmFilterCategory, FILTER_NAME_WIDE, CLSID_AviSynthFilter);

    filterMapper->Release();

    return hr;
}

}

CFactoryTemplate g_Templates[] = {
    { nullptr
    , &AvsFilter::CLSID_AviSynthFilter
    , AvsFilter::CreateInstance<AvsFilter::CAviSynthFilter>
    },

    { nullptr
    , &AvsFilter::CLSID_AvsPropSettings
    , AvsFilter::CreateInstance<AvsFilter::CAvsFilterPropSettings>
    },

    { nullptr
    , &AvsFilter::CLSID_AvsPropStatus
    , AvsFilter::CreateInstance<AvsFilter::CAvsFilterPropStatus>
    },
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

auto STDMETHODCALLTYPE DllRegisterServer() -> HRESULT {
    return AvsFilter::RegisterFilter();
}

auto STDMETHODCALLTYPE DllUnregisterServer() -> HRESULT {
    return AvsFilter::UnregisterFilter();
}

extern "C" DECLSPEC_NOINLINE BOOL WINAPI DllEntryPoint(HINSTANCE hInstance, ULONG ulReason, __inout_opt LPVOID pv);

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    return DllEntryPoint(hModule, ul_reason_for_call, lpReserved);
}
