#pragma once

#include <codeanalysis\warnings.h>
#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS 26812))

#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <cguid.h>
#include <initguid.h>
#include <shellapi.h>

// DirectShow BaseClasses
#include <streams.h>
#include <dvdmedia.h>

// AviSynth
#define AVS_LINKAGE_DLLIMPORT
#include <avisynth.h>

// intrinsics for fast (de-)interleaving array
#include <immintrin.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

//#define LOGGING

//#define MINIDUMP

#ifdef MINIDUMP
#include <client/windows/handler/exception_handler.h>
#endif

#include <D:\Setting\Profile\Desktop\Crash\CrashRpt_v.1.4.3_r1645\include\CrashRpt.h>

#pragma warning(pop)

#include "resource.h"