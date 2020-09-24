#include "pch.h"

#include "constants.h"
#include "filter.h"
#include "format.h"
#include "prop_settings.h"
#include "prop_status.h"
#include "logging.h"


#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif
#pragma comment(lib, "winmm.lib")

#pragma comment(lib, "AviSynth.lib")

static REGFILTERPINS PIN_REG[] = {
    { nullptr               // pin name (obsolete)
    , FALSE                 // is pin rendered?
    , FALSE                 // is this output pin?
    , FALSE                 // Can the filter create zero instances?
    , FALSE                 // Does the filter create multiple instances?
    , &CLSID_NULL           // filter CLSID the pin connects to (obsolete)
    , nullptr               // pin name the pin connects to (obsolete)
    , 0                     // pin media type count (to be filled in FillPinTypes())
    , nullptr },            // pin media types (to be filled in FillPinTypes())

    { nullptr               // pin name (obsolete)
    , FALSE                 // is pin rendered?
    , TRUE                  // is this output pin?
    , FALSE                 // Can the filter create zero instances?
    , FALSE                 // Does the filter create multiple instances?
    , &CLSID_NULL           // filter CLSID the pin connects to (obsolete)
    , nullptr               // pin name the pin connects to (obsolete)
    , 0                     // pin media type count (to be filled in FillPinTypes())
    , nullptr },            // pin media types (to be filled in FillPinTypes())
};

static constexpr ULONG PIN_COUNT = sizeof(PIN_REG) / sizeof(PIN_REG[0]);

static constexpr AMOVIESETUP_FILTER FILTER_REG = {
    &AvsFilter::CLSID_AviSynthFilter,  // filter CLSID
    FILTER_NAME_WIDE,                  // filter name
    MERIT_DO_NOT_USE,                  // filter merit
    PIN_COUNT,                         // pin count
    PIN_REG                            // pin information
};

static std::vector<REGPINTYPES> g_PinTypes;

static void FillPinTypes() {
    for (const AvsFilter::Format::Definition &info : AvsFilter::Format::DEFINITIONS) {
        g_PinTypes.emplace_back(REGPINTYPES { &MEDIATYPE_Video, &info.mediaSubtype });
    }

    PIN_REG[0].nMediaTypes = static_cast<UINT>(g_PinTypes.size());
    PIN_REG[0].lpMediaType = g_PinTypes.data();
    PIN_REG[1].nMediaTypes = static_cast<UINT>(g_PinTypes.size());
    PIN_REG[1].lpMediaType = g_PinTypes.data();
}

//#define LOGGING
//#define MINIDUMP

#ifdef LOGGING
static FILE *g_logFile = nullptr;
static DWORD g_logStartTime;
static std::mutex g_logMutex;
static char *g_loc = setlocale(LC_CTYPE, ".utf8");
#endif

auto AvsFilter::Log(const char *format, ...) -> void {
#ifdef LOGGING
    std::unique_lock<std::mutex> srcLock(g_logMutex);

    fprintf_s(g_logFile, "T %6i @ %8i: ", GetCurrentThreadId(), timeGetTime() - g_logStartTime);

    va_list args;
    va_start(args, format);
    vfprintf_s(g_logFile, format, args);
    va_end(args);

    fputc('\n', g_logFile);

    fflush(g_logFile);
#endif
}

#ifdef MINIDUMP
#include <client/windows/handler/exception_handler.h>

static google_breakpad::ExceptionHandler *g_exHandler;
#endif

static void CALLBACK InitRoutine(BOOL bLoading, const CLSID *rclsid) {
    if (bLoading == TRUE) {
        FillPinTypes();

#ifdef LOGGING
        g_logFile = _fsopen("C:\\avisynth_filter.log", "w", _SH_DENYNO);
        g_logStartTime = timeGetTime();
#endif // LOGGING

#ifdef MINIDUMP
        g_exHandler = new google_breakpad::ExceptionHandler(L".", nullptr, nullptr, nullptr, google_breakpad::ExceptionHandler::HANDLER_EXCEPTION, MiniDumpWithIndirectlyReferencedMemory, static_cast<HANDLE>(nullptr), nullptr);
#endif // MINIDUMP
    } else {
#ifdef MINIDUMP
        delete g_exHandler;
#endif // MINIDUMP

#ifdef LOGGING
        if (g_logFile != nullptr) {
            fclose(g_logFile);
        }
#endif
    }
}

template <typename T>
static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    CUnknown *newInstance = new T(pUnk, phr);

    if (newInstance == nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newInstance;
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

extern "C" DECLSPEC_NOINLINE BOOL WINAPI DllEntryPoint(HINSTANCE hInstance, ULONG ulReason, LPVOID pv);

auto APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) -> BOOL {
    return DllEntryPoint(hModule, ul_reason_for_call, lpReserved);
}
