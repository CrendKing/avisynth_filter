// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace SynthFilter {

/**
 * All requests and responses are transmitted through WM_COPYDATA message.
 * To access the API, activate the remote control module by setting the registry value REGISTRY_VALUE_NAME_REMOTE_CONTROL to non-zero.
 * The return value of each API is either noted as their output value, specified in the documentation, or TRUE if call is successful and FALSE if failed.
 * All strings are encoded in UTF-8.
 */

enum class AvsState {
    Stopped = 0,
    Running,
    Paused,
    Error,
};

namespace {

constexpr const int API_VERSION                           = 1;
constexpr const char *API_WND_CLASS_NAME                  = "AvsFilterRemoteControlClass";
constexpr const char *API_CSV_DELIMITER                   = ";";

// scale the fractional numbers by a factor and convert to integer for API messaging
constexpr const int PAR_SCALE_FACTOR                      = 1000;
constexpr const int FRAME_RATE_SCALE_FACTOR               = 1000;

////// filter related messages //////

/**
 * input : none
 * output: current API version in integer
 */
constexpr const ULONG_PTR API_MSG_GET_API_VERSION         = 100;

/**
 * input : none
 * output: list of video filter names joined by the delimiter
 * note  : the order of filters is preserved
 */
constexpr const ULONG_PTR API_MSG_GET_VIDEO_FILTERS       = 101;

////// input related messages //////

/**
 * input : none
 * output: input video's width
 */
constexpr const ULONG_PTR API_MSG_GET_INPUT_WIDTH         = 200;

/**
 * input : none
 * output: input video's height
 */
constexpr const ULONG_PTR API_MSG_GET_INPUT_HEIGHT        = 201;

/**
 * input : none
 * output: input video's pixel aspect ratio, scaled by PAR_SCALE_FACTOR
 */
constexpr const ULONG_PTR API_MSG_GET_INPUT_PAR           = 202;

/**
 * input : none
 * output: current input video's frames per second, scaled by FRAME_RATE_SCALE_FACTOR
 * note  : unlike API_MSG_GET_SOURCE_AVG_FPS, this FPS could vary if the input video
 *         has variable frame rate, or source filter delivers sample in stuttering way
 */
constexpr const ULONG_PTR API_MSG_GET_CURRENT_INPUT_FPS   = 203;

/**
 * input : none
 * output: input video's source path
 */
constexpr const ULONG_PTR API_MSG_GET_INPUT_SOURCE_PATH   = 204;

/**
 * input : none
 * output: input video's codec in FourCC
 */
constexpr const ULONG_PTR API_MSG_GET_INPUT_CODEC         = 205;

/**
 * input : none
 * output: input video's HDR type
 * note  : 0 means no HDR, 1 means has HDR
 */
constexpr const ULONG_PTR API_MSG_GET_INPUT_HDR_TYPE      = 206;

/**
 * input : none
 * output: input video's HDR liminance, in cd/m2
 */
constexpr const ULONG_PTR API_MSG_GET_INPUT_HDR_LUMINANCE = 207;

/**
 * input : none
 * output: average frames per second from the source video, scaled by FRAME_RATE_SCALE_FACTOR
 */
constexpr const ULONG_PTR API_MSG_GET_SOURCE_AVG_FPS      = 208;

////// output related messages //////

/**
 * input : none
 * output: current output video's frames per second, scaled by FRAME_RATE_SCALE_FACTOR
 */
constexpr const ULONG_PTR API_MSG_GET_CURRENT_OUTPUT_FPS  = 300;

////// FrameServer related messages //////

/**
 * input : none
 * output: current FrameServer environment state, in AvsState
 */
constexpr const ULONG_PTR API_MSG_GET_AVS_STATE           = 400;

/**
 * input : none
 * output: current FrameServer environment error message, if available
 * return: FALSE if there is no error, TRUE otherwise
 */
constexpr const ULONG_PTR API_MSG_GET_AVS_ERROR           = 401;

/**
 * input : none
 * output: effective FrameServer source file path, if available
 * return: FALSE if FrameServer script file is effective, TRUE otherwise
 */
constexpr const ULONG_PTR API_MSG_GET_AVS_SOURCE_FILE     = 402;

/**
 * input : FrameServer script file path
 * output: none
 */
constexpr const ULONG_PTR API_MSG_SET_AVS_SOURCE_FILE     = 403;

}
}
