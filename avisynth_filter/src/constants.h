#pragma once

#include "pch.h"


// {E5E2C1A6-C90F-4247-8BF5-604FB180A932}
DEFINE_GUID(CLSID_AviSynthFilter,
            0xe5e2c1a6, 0xc90f, 0x4247, 0x8b, 0xf5, 0x60, 0x4f, 0xb1, 0x80, 0xa9, 0x32);

// {90C56868-7D47-4AA2-A42D-06406A6DB35F}
DEFINE_GUID(CLSID_AvsPropertyPage,
            0x90c56868, 0x7d47, 0x4aa2, 0xa4, 0x2d, 0x6, 0x40, 0x6a, 0x6d, 0xb3, 0x5f);

// {BC067674-9400-4119-925D-49CDF689F79C}
DEFINE_GUID(CLSID_AvsFilterSettings,
            0xbc067674, 0x9400, 0x4119, 0x92, 0x5d, 0x49, 0xcd, 0xf6, 0x89, 0xf7, 0x9c);

// {871B4CAB-7E31-4E2C-8A9B-ED9AD64702DF}
DEFINE_GUID(IID_IAvsFilterSettings,
            0x871b4cab, 0x7e31, 0x4e2c, 0x8a, 0x9b, 0xed, 0x9a, 0xd6, 0x47, 0x2, 0xdf);

#define WidenHelper(str)  L##str
#define Widen(str)        WidenHelper(str)

#ifdef _DEBUG
#define FILTER_NAME_SUFFIX " [Debug]"
#else
#define FILTER_NAME_SUFFIX
#endif // DEBUG
#define PROPERTY_PAGE_SUFFIX " Property Page"
#define SETTINGS_SUFFIX " Settings"

#define FILTER_NAME_BASE "AviSynth Filter"
#define FILTER_NAME_FULL FILTER_NAME_BASE FILTER_NAME_SUFFIX
#define PROPERTY_PAGE_FULL FILTER_NAME_BASE PROPERTY_PAGE_SUFFIX FILTER_NAME_SUFFIX
#define SETTINGS_FULL FILTER_NAME_BASE SETTINGS_SUFFIX FILTER_NAME_SUFFIX

constexpr char *FILTER_NAME = FILTER_NAME_FULL;
constexpr wchar_t *FILTER_NAME_WIDE = Widen(FILTER_NAME_FULL);

constexpr char *PROPERTY_PAGE_NAME = PROPERTY_PAGE_FULL;
constexpr wchar_t *PROPERTY_PAGE_NAME_WIDE = Widen(PROPERTY_PAGE_FULL);

constexpr char *SETTINGS_NAME = SETTINGS_FULL;
constexpr wchar_t *SETTINGS_NAME_WIDE = Widen(SETTINGS_FULL);

constexpr char *EVAL_FILENAME = "avisynth_filter_script";

/*
 * Some source filter may not set the VIDEOINFOHEADER::AvgTimePerFrame field.
 * Default to 25 FPS in such cases.
 */
constexpr REFERENCE_TIME DEFAULT_AVG_TIME_PER_FRAME = 400000;

/*
 * Stream could last forever. Use a large power as the fake number of frames.
 * Avoid using too large number because some AviSynth filters allocate memory based on the number of frames.
 * Also, some filters may perform calculation on it, resulting overflow.
 * Same as ffdshow, uses a highly composite number 10810800, which could last 50 hours for a 60fps stream.
 */
constexpr int NUM_FRAMES_FOR_INFINITE_STREAM = 10810800;

// Special tag to reset delivery thread's current frame number to the input sample frame number.
constexpr int DELIVER_FRAME_NB_RESET = -1;

constexpr DWORD REGISTRY_INVALID_NUMBER = DWORD_MAX;
constexpr char *REGISTRY_KEY_NAME = "Software\\AviSynthFilter";
constexpr char *REGISTRY_VALUE_NAME_AVS_FILE = "AvsFile";
constexpr char *REGISTRY_VALUE_NAME_BUFFER_BACK = "BufferBack";
constexpr char *REGISTRY_VALUE_NAME_BUFFER_AHEAD = "BufferAhead";
constexpr char *REGISTRY_VALUE_NAME_FORMATS = "Formats";

// 30323449-0000-0010-8000-00AA00389B71  'I420' == MEDIASUBTYPE_I420
constexpr GUID MEDIASUBTYPE_I420 =
{ 0x30323449, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71} };

constexpr int BUFFER_FRAMES_MIN = 0;
constexpr int BUFFER_FRAMES_MAX = 100;
constexpr int BUFFER_BACK_DEFAULT = 0;
constexpr int BUFFER_AHEAD_DEFAULT = 10;
