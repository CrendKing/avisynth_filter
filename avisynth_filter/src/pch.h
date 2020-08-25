#pragma once

#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS 26812)

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <numeric>
#include <unordered_map>

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

#pragma warning(pop)

#include "resource.h"
