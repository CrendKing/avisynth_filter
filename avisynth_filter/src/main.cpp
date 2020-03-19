#include "pch.h"
#include "constants.h"
#include "filter.h"
#include "filter_prop.h"
#include "settings.h"


#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif
#pragma comment(lib, "winmm.lib")

#pragma comment(lib, "AviSynth.lib")

#ifdef MINIDUMP
static google_breakpad::ExceptionHandler *g_exHandler;
#endif // MINIDUMP

#ifdef LOGGING
FILE *g_loggingStream = nullptr;
#endif // LOGGING

static void CALLBACK InitRoutine(BOOL bLoading, const CLSID *rclsid) {
    if (bLoading == TRUE) {
#ifdef MINIDUMP
        g_exHandler = new google_breakpad::ExceptionHandler(L".", nullptr, nullptr, nullptr, google_breakpad::ExceptionHandler::HANDLER_EXCEPTION, MiniDumpWithIndirectlyReferencedMemory, L"", nullptr);
#endif // MINIDUMP

#ifdef LOGGING
        freopen_s(&g_loggingStream, "C:\\avs.log", "a", stdout);
#endif // LOGGING
    } else {
#ifdef LOGGING
        if (g_loggingStream != nullptr) {
            fclose(g_loggingStream);
        }
#endif // LOGGING

#ifdef MINIDUMP
        delete g_exHandler;
#endif // MINIDUMP
  }
}

static constexpr REGPINTYPES PIN_TYPE_REG[] = {
    { &MEDIATYPE_Video, &MEDIASUBTYPE_NV12 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_YV12 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_I420 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_IYUV },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P010 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_P016 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_YUY2 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_UYVY },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_RGB24 },
    { &MEDIATYPE_Video, &MEDIASUBTYPE_RGB32 },
};
static constexpr UINT PIN_TYPE_COUNT = sizeof(PIN_TYPE_REG) / sizeof(PIN_TYPE_REG[0]);

static constexpr REGFILTERPINS PIN_REG[] = {
    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , FALSE                // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , PIN_TYPE_COUNT       // pin media type count
    , PIN_TYPE_REG },      // pin media types

    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , TRUE                 // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , PIN_TYPE_COUNT       // pin media type count
    , PIN_TYPE_REG },      // pin media types
};

static constexpr ULONG PIN_COUNT = sizeof(PIN_REG) / sizeof(PIN_REG[0]);

static constexpr AMOVIESETUP_FILTER FILTER_REG = {
    &CLSID_AviSynthFilter,  // filter CLSID
    FILTER_NAME_WIDE,       // filter name
    MERIT_DO_NOT_USE,       // filter merit
    PIN_COUNT,              // pin count
    PIN_REG                 // pin information
};

CFactoryTemplate g_Templates[] = {
    { FILTER_NAME_WIDE
    , &CLSID_AviSynthFilter
    , CAviSynthFilter::CreateInstance
    , InitRoutine
    , &FILTER_REG },

    { PROPERTY_PAGE_NAME_WIDE
    , &CLSID_AvsPropertyPage
    , CAviSynthFilterProp::CreateInstance
    , nullptr
    , nullptr },

    { SETTINGS_NAME_WIDE
    , &CLSID_AvsFilterSettings
    , CAvsFilterSettings::CreateInstance
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