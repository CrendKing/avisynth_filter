#pragma once

#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)

#include "min_windows_macros.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <clocale>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <regex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#define _ATL_APARTMENT_THREADED
#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#include <atlbase.h>
#include <cguid.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dxva.h>
#include <immintrin.h>
#include <initguid.h>
#include <intrin.h>
#include <processthreadsapi.h>
#include <shellapi.h>

// DirectShow BaseClasses
#include <dvdmedia.h>
#include <streams.h>

#ifdef AVSF_AVISYNTH
    #include <avisynth.h>
#else
    #include <VSHelper4.h>
    #include <VSScript4.h>
    #include <VapourSynth4.h>
#endif
#include <SimpleIni.h>
#include <VSConstants4.h>

#pragma warning(pop)

#include "resource.h"

#pragma warning(push)
#pragma warning(disable: 26495 26812)
