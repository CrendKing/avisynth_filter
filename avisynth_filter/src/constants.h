// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "version.h"


namespace AvsFilter {

// {E5E2C1A6-C90F-4247-8BF5-604FB180A932}
DEFINE_GUID(inline CLSID_AviSynthFilter,
            0xe5e2c1a6, 0xc90f, 0x4247, 0x8b, 0xf5, 0x60, 0x4f, 0xb1, 0x80, 0xa9, 0x32);

// {90C56868-7D47-4AA2-A42D-06406A6DB35F}
DEFINE_GUID(inline CLSID_AvsPropSettings,
            0x90c56868, 0x7d47, 0x4aa2, 0xa4, 0x2d, 0x06, 0x40, 0x6a, 0x6d, 0xb3, 0x5f);

// {E58206EF-C9F2-4F8C-BF0B-975C28552700}
DEFINE_GUID(inline CLSID_AvsPropStatus,
            0xe58206ef, 0xc9f2, 0x4f8c, 0xbf, 0x0b, 0x97, 0x5c, 0x28, 0x55, 0x27, 0x00);

// {85C582FE-5B6F-4BE5-A0B0-57A4FDAB4412}
DEFINE_GUID(inline IID_IAvsFilter,
            0x85c582fe, 0x5b6f, 0x4be5, 0xa0, 0xb0, 0x57, 0xa4, 0xfd, 0xab, 0x44, 0x12);

static const GUID MEDIASUBTYPE_I420 = FOURCCMap('024I');
static const GUID MEDIASUBTYPE_YV24 = FOURCCMap('42VY');

#define WidenHelper(str)                  L##str
#define Widen(str)                        WidenHelper(str)

#ifdef _DEBUG
#define FILTER_NAME_SUFFIX                " [Debug]"
#else
#define FILTER_NAME_SUFFIX
#endif // DEBUG
#define SETTINGS_SUFFIX                   " Settings"
#define STATUS_SUFFIX                     " Status"

#define FILTER_NAME_FULL FILTER_NAME_BASE FILTER_NAME_SUFFIX
#define SETTINGS_FULL FILTER_NAME_BASE    SETTINGS_SUFFIX FILTER_NAME_SUFFIX
#define STATUS_FULL FILTER_NAME_BASE      STATUS_SUFFIX FILTER_NAME_SUFFIX

#define FILTER_NAME_WIDE                  Widen(FILTER_NAME_FULL)
#define SETTINGS_WIDE                     Widen(SETTINGS_FULL)
#define STATUS_WIDE                       Widen(STATUS_FULL)

// interface version 7 = AviSynth+ 3.5
static constexpr int MINIMUM_AVISYNTH_PLUS_INTERFACE_VERSION     = 7;

/*
 * Max number of frames received before blocking upstream from flooding the input queue.
 * Once reached, it must wait until the output thread delivers and GC source frames.
 */
static constexpr int MAX_SOURCE_FRAMES_AHEAD_OF_DELIVERY         = 7;

/*
 * If an output frame's stop time is this value close to the the next source frame's
 * start time, make up its stop time with the padding.
 * This trick helps eliminating frame time drift due to precision loss.
 * Unit is 100ns. 10 = 1ms.
 */
static constexpr int MAX_OUTPUT_FRAME_DURATION_PADDING           = 10;

static constexpr int DEFAULT_OUTPUT_SAMPLE_WORKER_THREAD_COUNT   = 1;

/*
 * When (de-)interleaving the data from the buffers for U and V planes, if the stride is
 * not a multiple of sizeof(__m128i), we can't use intrinsics to bulk copy the remainder bytes.
 * Traditionally we copy these bytes in a loop.
 *
 * However, if we allocate the buffer size with some headroom, we can keep using the same
 * logic with intrinsics, simplifying the code. The junk data in the padding will be harmless.
 *
 * Maximum padding is needed when there are minimum remainder bytes, which is 1 byte for each
 * U and V planes (total 2 bytes). Thus padding size is:
 *
 * size of data taken by the intrinsic per interation - 2
 */
static constexpr int INPUT_MEDIA_SAMPLE_BUFFER_PADDING = sizeof(__m128i) - 2;
static constexpr int OUTPUT_MEDIA_SAMPLE_BUFFER_PADDING = sizeof(__m128i) * 2 - 2;

/*
 * Some source filter may not set the VIDEOINFOHEADER::AvgTimePerFrame field.
 * Default to 25 FPS in such cases.
 */
static constexpr REFERENCE_TIME DEFAULT_AVG_TIME_PER_FRAME       = 400000;
static constexpr int STATUS_PAGE_TIMER_INTERVAL_MS               = 1000;
static constexpr const wchar_t *UNAVAILABLE_SOURCE_PATH          = L"N/A";

/*
 * Stream could last forever. Use a large power as the fake number of frames.
 * Avoid using too large number because some AviSynth filters allocate memory based on the number of frames.
 * Also, some filters may perform calculation on it, resulting overflow.
 * Same as ffdshow, uses a highly composite number 10810800, which could last 50 hours for a 60fps stream.
 */
static constexpr int NUM_FRAMES_FOR_INFINITE_STREAM              = 10810800;

static constexpr const wchar_t *REGISTRY_KEY_NAME                = L"Software\\AviSynthFilter";
static constexpr const wchar_t *SETTING_NAME_AVS_FILE            = L"AvsFile";
static constexpr const wchar_t *SETTING_NAME_LOG_FILE            = L"LogFile";
static constexpr const wchar_t *SETTING_NAME_INPUT_FORMAT_PREFIX = L"InputFormat_";
static constexpr const wchar_t *SETTING_NAME_OUTPUT_THREADS      = L"OutputThreads";
static constexpr const wchar_t *SETTING_NAME_REMOTE_CONTROL      = L"RemoteControl";

static constexpr int REMOTE_CONTROL_SMTO_TIMEOUT_MS              = 1000;

}
