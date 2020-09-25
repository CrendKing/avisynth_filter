#pragma once

#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS 26812)

#include <clocale>
#include <deque>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
#define AVS_LINKAGE_DLLIMPORT
#include <avisynth.h>

#pragma warning(pop)

#include "resource.h"
