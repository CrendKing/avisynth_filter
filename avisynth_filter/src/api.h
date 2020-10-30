#pragma once

#include "pch.h"


namespace AvsFilter {

/**
 * All requests and responses are transmitted through WM_COPYDATA message.
 * To access the API, activate the remote control module by setting the registry value REGISTRY_VALUE_NAME_REMOTE_CONTROL to non-zero.
 * The return value of each API is either noted as their output value, specified in the documentation, or TRUE if call is successful and FALSE if failed.
 */

enum class AvsState {
    Stopped = 0,
    Running,
    Paused,
    Error
};

static constexpr int API_VERSION                           = 1;
static constexpr char *API_CLASS_NAME                      = "AvsFilterRemoteControlClass";
static constexpr char API_CSV_DELIMITER                    = ';';

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
 * output: current input video's frames per second, scaled by FRAME_RATE_SCALE_FACTOR
 * note  : unlike API_MSG_GET_SOURCE_AVG_FPS, this FPS could vary if the input video
 *         has variable frame rate, or source filter delivers sample in stuttering way
 */
static constexpr ULONG_PTR API_MSG_GET_CURRENT_INPUT_FPS   = 203;

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

/**
 * input : none
 * output: average frames per second from the source video, scaled by FRAME_RATE_SCALE_FACTOR
 */
static constexpr ULONG_PTR API_MSG_GET_SOURCE_AVG_FPS      = 208;
                                                           
////// output related messages //////

/**
 * input : none
 * output: current output video's frames per second, scaled by FRAME_RATE_SCALE_FACTOR
 */
static constexpr ULONG_PTR API_MSG_GET_CURRENT_OUTPUT_FPS  = 300;
                                                           
////// AviSynth related messages //////

/**
 * input : none
 * output: current AviSynth environment state, in AvsState
 */
static constexpr ULONG_PTR API_MSG_GET_AVS_STATE           = 400;

/**
 * input : none
 * output: current AviSynth environment error message, if available
 * return: FALSE if there is no error, TRUE otherwise
 */
static constexpr ULONG_PTR API_MSG_GET_AVS_ERROR           = 401;

/**
 * input : none
 * output: effective AviSynth source file path, if available
 * return: FALSE if AviSynth source file is effective, TRUE otherwise
 */
static constexpr ULONG_PTR API_MSG_GET_AVS_SOURCE_FILE = 402;

/**
 * input : AviSynth source file path
 * output: none
 */
static constexpr ULONG_PTR API_MSG_SET_AVS_SOURCE_FILE     = 403;

}
