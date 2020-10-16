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

template <typename T>
static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    if (std::is_same_v<T, AvsFilter::CAviSynthFilter> && !AvsFilter::g_env.Initialize(phr)) {
        return nullptr;
    }
        
    CUnknown *newInstance = new T(pUnk, phr);
    if (newInstance == nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newInstance;
}

static REGFILTERPINS PIN_REG[] = {
    { nullptr               // pin name (obsolete)
    , FALSE                 // is pin rendered?
    , FALSE                 // is this output pin?
    , FALSE                 // Can the filter create zero instances?
    , FALSE                 // Does the filter create multiple instances?
    , &CLSID_NULL           // filter CLSID the pin connects to (obsolete)
    , nullptr               // pin name the pin connects to (obsolete)
    , 0                     // pin media type count (to be filled in InitRoutine())
    , nullptr },            // pin media types (to be filled in InitRoutine())

    { nullptr               // pin name (obsolete)
    , FALSE                 // is pin rendered?
    , TRUE                  // is this output pin?
    , FALSE                 // Can the filter create zero instances?
    , FALSE                 // Does the filter create multiple instances?
    , &CLSID_NULL           // filter CLSID the pin connects to (obsolete)
    , nullptr               // pin name the pin connects to (obsolete)
    , 0                     // pin media type count (to be filled in InitRoutine())
    , nullptr },            // pin media types (to be filled in InitRoutine())
};

static constexpr ULONG PIN_COUNT = sizeof(PIN_REG) / sizeof(PIN_REG[0]);

static constexpr AMOVIESETUP_FILTER FILTER_REG = {
    &AvsFilter::CLSID_AviSynthFilter,  // filter CLSID
    FILTER_NAME_WIDE,                  // filter name
    MERIT_DO_NOT_USE,                  // filter merit
    PIN_COUNT,                         // pin count
    PIN_REG                            // pin information
};

//#define MINIDUMP

#ifdef MINIDUMP
#include <client/windows/handler/exception_handler.h>

static google_breakpad::ExceptionHandler *g_exHandler;
#endif

static auto CALLBACK InitRoutine(BOOL bLoading, const CLSID *rclsid) -> void {
    if (bLoading == TRUE) {
        static std::vector<REGPINTYPES> pinTypes;

        for (const AvsFilter::Format::Definition &info : AvsFilter::Format::DEFINITIONS) {
            pinTypes.emplace_back(REGPINTYPES { &MEDIATYPE_Video, &info.mediaSubtype });
        }

        PIN_REG[0].nMediaTypes = static_cast<UINT>(pinTypes.size());
        PIN_REG[0].lpMediaType = pinTypes.data();
        PIN_REG[1].nMediaTypes = static_cast<UINT>(pinTypes.size());
        PIN_REG[1].lpMediaType = pinTypes.data();

#ifdef MINIDUMP
        g_exHandler = new google_breakpad::ExceptionHandler(L".", nullptr, nullptr, nullptr, google_breakpad::ExceptionHandler::HANDLER_EXCEPTION, MiniDumpWithIndirectlyReferencedMemory, static_cast<HANDLE>(nullptr), nullptr);
#endif // MINIDUMP
    } else {

#ifdef MINIDUMP
        delete g_exHandler;
#endif // MINIDUMP
    }
}

CFactoryTemplate g_Templates[] = {
    { FILTER_NAME_WIDE
    , &AvsFilter::CLSID_AviSynthFilter
    , CreateInstance<AvsFilter::CAviSynthFilter>
    , InitRoutine
    , &FILTER_REG },

    { SETTINGS_WIDE
    , &AvsFilter::CLSID_AvsPropSettings
    , CreateInstance<AvsFilter::CAvsFilterPropSettings>
    , nullptr
    , nullptr },

    { STATUS_WIDE
    , &AvsFilter::CLSID_AvsPropStatus
    , CreateInstance<AvsFilter::CAvsFilterPropStatus>
    , nullptr
    , nullptr },
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

STDAPI DllRegisterServer() {
    return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer() {
    return AMovieDllRegisterServer2(FALSE);
}

extern "C" DECLSPEC_NOINLINE BOOL WINAPI DllEntryPoint(HINSTANCE hInstance, ULONG ulReason, __inout_opt LPVOID pv);

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    return DllEntryPoint(hModule, ul_reason_for_call, lpReserved);
}
