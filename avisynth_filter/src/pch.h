#pragma once

#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <cguid.h>
#include <initguid.h>
#include <shellapi.h>

// baseclasses
#include <streams.h>
#include <dvdmedia.h>

// AviSynth
#define AVS_LINKAGE_DLLIMPORT
#include <avisynth.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "resource.h"

#ifdef _DEBUG
#define LOGGING
#endif