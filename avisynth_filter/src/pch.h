#pragma once

#include <codeanalysis/warnings.h>
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

#include <client/windows/handler/exception_handler.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma warning(pop)

#include "resource.h"