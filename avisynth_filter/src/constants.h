#pragma once

#include "pch.h"


// {E5E2C1A6-C90F-4247-8BF5-604FB180A932}
DEFINE_GUID(CLSID_AviSynthFilter,
            0xe5e2c1a6, 0xc90f, 0x4247, 0x8b, 0xf5, 0x60, 0x4f, 0xb1, 0x80, 0xa9, 0x32);

// {90C56868-7D47-4AA2-A42D-06406A6DB35F}
DEFINE_GUID(CLSID_AvsPropSettings,
            0x90c56868, 0x7d47, 0x4aa2, 0xa4, 0x2d, 0x06, 0x40, 0x6a, 0x6d, 0xb3, 0x5f);

// {E58206EF-C9F2-4F8C-BF0B-975C28552700}
DEFINE_GUID(CLSID_AvsPropStatus,
            0xe58206ef, 0xc9f2, 0x4f8c, 0xbf, 0x0b, 0x97, 0x5c, 0x28, 0x55, 0x27, 0x00);

// {871B4CAB-7E31-4E2C-8A9B-ED9AD64702DF}
DEFINE_GUID(IID_IAvsFilterSettings,
            0x871b4cab, 0x7e31, 0x4e2c, 0x8a, 0x9b, 0xed, 0x9a, 0xd6, 0x47, 0x02, 0xdf);

// {2A5B2CEC-D874-4ED8-B8D9-4043333037E4}
DEFINE_GUID(IID_IAvsFilterStatus,
            0x2a5b2cec, 0xd874, 0x4ed8, 0xb8, 0xd9, 0x40, 0x43, 0x33, 0x30, 0x37, 0xe4);

#define WidenHelper(str)  L##str
#define Widen(str)        WidenHelper(str)

#ifdef _DEBUG
#define FILTER_NAME_SUFFIX " [Debug]"
#else
#define FILTER_NAME_SUFFIX
#endif // DEBUG
#define SETTINGS_SUFFIX " Settings"
#define STATUS_SUFFIX " Status"

#define FILTER_NAME_BASE "AviSynth Filter"

#define FILTER_NAME_FULL FILTER_NAME_BASE FILTER_NAME_SUFFIX
#define SETTINGS_FULL FILTER_NAME_BASE SETTINGS_SUFFIX FILTER_NAME_SUFFIX
#define STATUS_FULL FILTER_NAME_BASE STATUS_SUFFIX FILTER_NAME_SUFFIX

#define FILTER_NAME_WIDE Widen(FILTER_NAME_FULL)
#define SETTINGS_WIDE Widen(SETTINGS_FULL)
#define STATUS_WIDE Widen(STATUS_FULL)

/*
 * Some source filter may not set the VIDEOINFOHEADER::AvgTimePerFrame field.
 * Default to 25 FPS in such cases.
 */
constexpr REFERENCE_TIME DEFAULT_AVG_TIME_PER_FRAME = 400000;

constexpr char *EVAL_FILENAME = "avisynth_filter_script";

constexpr int INVALID_DEFINITION = -1;

/*
 * Stream could last forever. Use a large power as the fake number of frames.
 * Avoid using too large number because some AviSynth filters allocate memory based on the number of frames.
 * Also, some filters may perform calculation on it, resulting overflow.
 * Same as ffdshow, uses a highly composite number 10810800, which could last 50 hours for a 60fps stream.
 */
constexpr int NUM_FRAMES_FOR_INFINITE_STREAM = 10810800;

constexpr wchar_t *REGISTRY_KEY_NAME = L"Software\\AviSynthFilter";
constexpr wchar_t *REGISTRY_VALUE_NAME_AVS_FILE = L"AvsFile";
constexpr wchar_t *REGISTRY_VALUE_NAME_FORMATS = L"Formats";

const GUID MEDIASUBTYPE_I420 = FOURCCMap('024I');
const GUID MEDIASUBTYPE_RGB48 = FOURCCMap('0BGR');