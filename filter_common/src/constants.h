// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "version.h"


namespace SynthFilter {

namespace {

const GUID MEDIASUBTYPE_I420                                  = FOURCCMap('024I');
const GUID MEDIASUBTYPE_YV24                                  = FOURCCMap('42VY');
const GUID MEDIASUBTYPE_Y410                                  = FOURCCMap('014Y');
const GUID MEDIASUBTYPE_Y416                                  = FOURCCMap('614Y');

#define SETTINGS_NAME_SUFFIX                                    " Settings"
#define STATUS_NAME_SUFFIX                                      " Status"

#define FILTER_NAME_FULL                                        FILTER_NAME_WIDE FILTER_VARIANT
#define SETTINGS_NAME_FULL                                      FILTER_NAME_WIDE SETTINGS_NAME_SUFFIX FILTER_VARIANT
#define STATUS_NAME_FULL                                        FILTER_NAME_WIDE STATUS_NAME_SUFFIX FILTER_VARIANT

// interface version 7 = AviSynth+ 3.5
constexpr const int MINIMUM_AVISYNTH_PLUS_INTERFACE_VERSION   = 7;
constexpr const DWORD SAMPLE2_TYPE_SPECIFIC_FLAGS_SIZE        = offsetof(AM_SAMPLE2_PROPERTIES, dwSampleFlags);

constexpr const double EXTRA_SRC_BUFFER_CHANGE_THRESHOLD      = 0.1;
constexpr const int INITIAL_SRC_BUFFER                        = 2;
constexpr const int MIN_EXTRA_SRC_BUFFER                      = 0;
constexpr const int EXTRA_SRC_BUFFER_DEC_STEP                 = 1;
constexpr const int MAX_EXTRA_SRC_BUFFER                      = 15;
constexpr const int EXTRA_SRC_BUFFER_INC_STEP                 = 2;

/*
 * If an output frame's stop time is this value close to the the next source frame's
 * start time, make up its stop time with the padding.
 * This trick helps eliminating frame time drift due to precision loss.
 * Unit is 100ns. 10 = 1ms.
 */
constexpr const int MAX_OUTPUT_FRAME_DURATION_PADDING         = 10;

/*
 * Some source filter may not set the VIDEOINFOHEADER::AvgTimePerFrame field.
 * Default to 25 FPS in such cases.
 */
constexpr const REFERENCE_TIME DEFAULT_AVG_TIME_PER_FRAME     = 400000;
constexpr const std::chrono::milliseconds STATUS_PAGE_TIMER_INTERVAL(1000);
constexpr const WCHAR *UNAVAILABLE_SOURCE_PATH                = L"N/A";

/*
 * Stream could last forever. Use a large power as the fake number of frames.
 * Avoid using too large number because some AviSynth filters allocate memory based on the number of frames.
 * Also, some filters may perform calculation on it, resulting overflow.
 * Same as ffdshow, uses a highly composite number 10810800, which could last 50 hours for a 60fps stream.
 */
constexpr const int NUM_FRAMES_FOR_INFINITE_STREAM            = 10810800;

/*
 * align stride of input media type to this number so that LAV Filters can enable its "direct" mode
 * for better performance.
 */
constexpr const int MEDIA_SAMPLE_STRIDE_ALGINMENT             = 32;

/*
 * AviSynth+ and VapourSynth frame property names
 * The ones prefixed with "AVSF_" are specific private properties of this filter, both variants
 */
constexpr const char *FRAME_PROP_NAME_ABS_TIME                = "_AbsoluteTime";
constexpr const char *FRAME_PROP_NAME_DURATION_NUM            = "_DurationNum";
constexpr const char *FRAME_PROP_NAME_DURATION_DEN            = "_DurationDen";
constexpr const char *FRAME_PROP_NAME_FIELD_BASED             = "_FieldBased";
constexpr const char *FRAME_PROP_NAME_SOURCE_FRAME_NB         = "AVSF_SourceFrameNb";
constexpr const char *FRAME_PROP_NAME_TYPE_SPECIFIC_FLAGS     = "AVSF_TypeSpecificFlags";

constexpr const WCHAR *REGISTRY_KEY_NAME_PREFIX               = L"Software\\AviSynthFilter\\";
constexpr const WCHAR *SETTING_NAME_SCRIPT_FILE               = L"ScriptFile";
constexpr const WCHAR *SETTING_NAME_LOG_FILE                  = L"LogFile";
constexpr const WCHAR *SETTING_NAME_INPUT_FORMAT_PREFIX       = L"InputFormat_";
constexpr const WCHAR *SETTING_NAME_REMOTE_CONTROL            = L"RemoteControl";
constexpr const WCHAR *SETTING_NAME_INITIAL_SRC_BUFFER        = L"InitialSrcBuffer";
constexpr const WCHAR *SETTING_NAME_MIN_EXTRA_SRC_BUFFER      = L"MinExtraSrcBuffer";
constexpr const WCHAR *SETTING_NAME_MAX_EXTRA_SRC_BUFFER      = L"MaxExtraSrcBuffer";
constexpr const WCHAR *SETTING_NAME_EXTRA_SRC_BUFFER_DEC_STEP = L"ExtraSrcBufferDecStep";
constexpr const WCHAR *SETTING_NAME_EXTRA_SRC_BUFFER_INC_STEP = L"ExtraSrcBufferIncStep";

constexpr const int REMOTE_CONTROL_SMTO_TIMEOUT_MS            = 1000;

}

}
