// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "version.h"


namespace SynthFilter {

static const GUID MEDIASUBTYPE_I420                                = FOURCCMap('024I');
static const GUID MEDIASUBTYPE_YV24                                = FOURCCMap('42VY');

#define SETTINGS_NAME_SUFFIX                                         " Settings"
#define STATUS_NAME_SUFFIX                                           " Status"

#define FILTER_NAME_FULL                                             FILTER_NAME_WIDE FILTER_VARIANT
#define SETTINGS_NAME_FULL                                           FILTER_NAME_WIDE SETTINGS_NAME_SUFFIX FILTER_VARIANT
#define STATUS_NAME_FULL                                             FILTER_NAME_WIDE STATUS_NAME_SUFFIX FILTER_VARIANT

// interface version 7 = AviSynth+ 3.5
static constexpr const int MINIMUM_AVISYNTH_PLUS_INTERFACE_VERSION = 7;

static constexpr const double EXTRA_SOURCE_BUFFER_INCREASE_THRESHOLD = 0.95;
static constexpr const double EXTRA_SOURCE_BUFFER_DECREASE_THRESHOLD = 1.05;
static constexpr const int MAXIMUM_EXTRA_SOURCE_BUFFER = 14;

/*
 * If an output frame's stop time is this value close to the the next source frame's
 * start time, make up its stop time with the padding.
 * This trick helps eliminating frame time drift due to precision loss.
 * Unit is 100ns. 10 = 1ms.
 */
static constexpr const int MAX_OUTPUT_FRAME_DURATION_PADDING       = 10;

/*
 * Some source filter may not set the VIDEOINFOHEADER::AvgTimePerFrame field.
 * Default to 25 FPS in such cases.
 */
static constexpr const REFERENCE_TIME DEFAULT_AVG_TIME_PER_FRAME   = 400000;
static constexpr const int STATUS_PAGE_TIMER_INTERVAL_MS           = 1000;
static constexpr const WCHAR *UNAVAILABLE_SOURCE_PATH              = L"N/A";

/*
 * Stream could last forever. Use a large power as the fake number of frames.
 * Avoid using too large number because some AviSynth filters allocate memory based on the number of frames.
 * Also, some filters may perform calculation on it, resulting overflow.
 * Same as ffdshow, uses a highly composite number 10810800, which could last 50 hours for a 60fps stream.
 */
static constexpr const int NUM_FRAMES_FOR_INFINITE_STREAM          = 10810800;

static constexpr const WCHAR *REGISTRY_KEY_NAME_PREFIX             = L"Software\\AviSynthFilter\\";
static constexpr const WCHAR *SETTING_NAME_SCRIPT_FILE             = L"ScriptFile";
static constexpr const WCHAR *SETTING_NAME_LOG_FILE                = L"LogFile";
static constexpr const WCHAR *SETTING_NAME_INPUT_FORMAT_PREFIX     = L"InputFormat_";
static constexpr const WCHAR *SETTING_NAME_REMOTE_CONTROL          = L"RemoteControl";
static constexpr const WCHAR *SETTING_NAME_EXTRA_SOURCE_BUFFER     = L"ExtraSourceBuffer";

static constexpr const int REMOTE_CONTROL_SMTO_TIMEOUT_MS          = 1000;

}
