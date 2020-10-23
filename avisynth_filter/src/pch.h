#pragma once

#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS 26812)

#include <clocale>
#include <condition_variable>
#include <deque>
#include <memory>
#include <numeric>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define _ATL_APARTMENT_THREADED
#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#include <atlbase.h>

#include <commctrl.h>
#include <commdlg.h>
#include <cguid.h>
#include <immintrin.h>
#include <initguid.h>
#include <processthreadsapi.h>
#include <shellapi.h>

// DirectShow BaseClasses
#include <streams.h>
#include <dvdmedia.h>

// AviSynth
#include <avisynth.h>

#pragma warning(pop)

#include "resource.h"
