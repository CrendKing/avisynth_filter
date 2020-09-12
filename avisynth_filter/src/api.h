#pragma once

#include "pch.h"


namespace AvsFilter {
	
/**
 * All requests and responses are transmitted through WM_COPYDATA message.
 * To access the API, activate the remote control module by creating the registry value defined by REGISTRY_VALUE_NAME_REMOTE_CONTROL.
 */

enum class AvsState {
    Stopped = 0,
    Running,
    Paused,
    Error
};

static constexpr int API_VERSION                           = 1;
static constexpr wchar_t *API_CLASS_NAME                   = L"AvsFilterRemoteControlClass";
static constexpr wchar_t API_CSV_DELIMITER                 = ';';

// scale the fractional numbers by a factor and convert to integer for API messaging
static constexpr int PAR_SCALE_FACTOR = 1000;
static constexpr int FRAME_RATE_SCALE_FACTOR = 1000;
                                                           
////// filter related messages //////

/**
 * input : none
 * output: current API version in integer
 */
static constexpr ULONG_PTR API_MSG_GET_API_VERSION         = 100;

/**
 * input : none
 * output: list of video filter names joined by the delimiter
 * note  : the order of filters is preserved
 */
static constexpr ULONG_PTR API_MSG_GET_VIDEO_FILTERS       = 101;
                                                           
////// input related messages //////

/**
 * input : none
 * output: input video's width
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_WIDTH         = 200;

/**
 * input : none
 * output: input video's height
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_HEIGHT        = 201;

/**
 * input : none
 * output: input video's pixel aspect ratio, scaled by PAR_SCALE_FACTOR
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_PAR           = 202;

/**
 * input : none
 * output: input video's frames per second, scaled by FRAME_RATE_SCALE_FACTOR
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_FPS           = 203;

/**
 * input : none
 * output: input video's source path
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_SOURCE_PATH   = 204;

/**
 * input : none
 * output: input video's codec in FourCC
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_CODEC         = 205;

/**
 * input : none
 * output: input video's HDR type
 * note  : 0 means no HDR, 1 means has HDR
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_HDR_TYPE      = 206;

/**
 * input : none
 * output: input video's HDR liminance, in cd/m2
 */
static constexpr ULONG_PTR API_MSG_GET_INPUT_HDR_LUMINANCE = 207;
                                                           
////// output related messages //////

/**
 * input : none
 * output: output video's frames per second, scaled by FRAME_RATE_SCALE_FACTOR
 */
static constexpr ULONG_PTR API_MSG_GET_OUTPUT_FPS          = 300;
                                                           
////// AviSynth related messages //////

/**
 * input : none
 * output: current AviSynth environment state, in AvsState
 */
static constexpr ULONG_PTR API_MSG_GET_AVS_STATE           = 400;

/**
 * input : none
 * output: current AviSynth environment error message, if available
 * return: 0 if there is no error, 1 otherwise
 */
static constexpr ULONG_PTR API_MSG_GET_AVS_ERROR           = 401;

/**
 * input : none
 * output: AviSynth source file path, if available
 * return: 0 if current AviSynth source is not a file, 1 otherwise
 */
static constexpr ULONG_PTR API_MSG_GET_AVS_SOURCE_FILE     = 402;

/**
 * input : AviSynth source file path
 * output: none
 */
static constexpr ULONG_PTR API_MSG_SET_AVS_SOURCE_FILE     = 403;

}