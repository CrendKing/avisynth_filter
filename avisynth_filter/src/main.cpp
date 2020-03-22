#include "pch.h"
#include "constants.h"
#include "filter.h"
#include "filter_prop.h"
#include "format.h"


#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif
#pragma comment(lib, "winmm.lib")

#pragma comment(lib, "AviSynth.lib")

#pragma comment(lib, "D:\\Setting\\Profile\\Desktop\\Crash\\CrashRpt_v.1.4.3_r1645\\lib\\CrashRpt1403.lib")
//#pragma comment(lib, "D:\\Setting\\Profile\\Desktop\\Crash\\CrashRpt_v.1.4.3_r1645\\lib\\x64\\CrashRpt1403.lib")

static REGFILTERPINS PIN_REG[] = {
    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , FALSE                // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , 0                    // pin media type count (to be filled in FillPinTypes())
    , nullptr },           // pin media types (to be filled in FillPinTypes())

    { nullptr              // pin name (obsolete)
    , FALSE                // is pin rendered?
    , TRUE                 // is this output pin?
    , FALSE                // Can the filter create zero instances?
    , FALSE                // Does the filter create multiple instances?
    , &CLSID_NULL          // filter CLSID the pin connects to (obsolete)
    , nullptr              // pin name the pin connects to (obsolete)
    , 0                    // pin media type count (to be filled in FillPinTypes())
    , nullptr },           // pin media types (to be filled in FillPinTypes())
};

static constexpr ULONG PIN_COUNT = sizeof(PIN_REG) / sizeof(PIN_REG[0]);

static constexpr AMOVIESETUP_FILTER FILTER_REG = {
    &CLSID_AviSynthFilter,  // filter CLSID
    FILTER_NAME_WIDE,       // filter name
    MERIT_DO_NOT_USE,       // filter merit
    PIN_COUNT,              // pin count
    PIN_REG                 // pin information
};

static std::vector<REGPINTYPES> g_PinTypes;

static void FillPinTypes() {
    for (const Format::FormatInfo &info : Format::FORMATS) {
        g_PinTypes.emplace_back(REGPINTYPES { &MEDIATYPE_Video, &info.mediaSubtype });
    }

    PIN_REG[0].nMediaTypes = static_cast<UINT>(g_PinTypes.size());
    PIN_REG[0].lpMediaType = g_PinTypes.data();
    PIN_REG[1].nMediaTypes = static_cast<UINT>(g_PinTypes.size());
    PIN_REG[1].lpMediaType = g_PinTypes.data();
}

#ifdef MINIDUMP
static google_breakpad::ExceptionHandler *g_exHandler;
#endif // MINIDUMP

#ifdef LOGGING
FILE *g_loggingStream = nullptr;
#endif // LOGGING

static void CALLBACK InitRoutine(BOOL bLoading, const CLSID *rclsid) {
    if (bLoading == TRUE) {
        FillPinTypes();

        /*CR_INSTALL_INFO cri {};
        cri.cb = sizeof(CR_INSTALL_INFO);
        cri.dwFlags = CR_INST_ALL_POSSIBLE_HANDLERS;
        crInstall(&cri);*/
#ifdef MINIDUMP
        g_exHandler = new google_breakpad::ExceptionHandler(L".", nullptr, nullptr, nullptr, google_breakpad::ExceptionHandler::HANDLER_EXCEPTION, MiniDumpWithIndirectlyReferencedMemory, L"", nullptr);
#endif // MINIDUMP

#ifdef LOGGING
        freopen_s(&g_loggingStream, ".\\avisynth_filter.log", "w", stdout);
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
        //crUninstall();
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
    , &CLSID_AviSynthFilter
    , CreateInstance<CAviSynthFilter>
    , InitRoutine
    , &FILTER_REG },

    { PROPERTY_PAGE_NAME_WIDE
    , &CLSID_AvsPropertyPage
    , CreateInstance<CAviSynthFilterProp>
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