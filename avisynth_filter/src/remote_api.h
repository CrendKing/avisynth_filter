#pragma once

#define AVSF_API_VERSION		1

// class name of the HWND_MESSAGE-window to communicate with
#define AVSF_CLASS_NAME         L"AVSFilterRC"

#define AVSF_MSG (WM_USER+1)		// uMsg value

#define AVSF_GET_API_VERSION	0	// returns AVSF_API_VERSION

#define AVSF_SET_AVS_FILE		1	// path to AVS script (UTF-8 string)
#define AVSF_GET_ERROR			2	// AVS error, if any (UTF-8 string)

#define AVSF_GET_PLAY_STATE		100 // 0 - initializing, 1 - script error, 2 - playing, 3 - paused
#define AVSF_GET_FILTERS		101 // ;-separated DS filters list (UTF-8 string)

#define AVSF_GET_FILENAME		200	// path / URL (UTF-8 string)
#define AVSF_GET_VIDEO_WIDTH	201 // source width, px
#define AVSF_GET_VIDEO_HEIGHT	202 // source height, px
#define AVSF_GET_VIDEO_PAR		203 // source PAR multiplied by 1000 (i.e. 1.5 -> 1500)
#define AVSF_GET_VIDEO_FPS		204 // measured source FPS multipled by 1000
#define AVSF_GET_VIDEO_COLOR	205 // FOURCC of source color space
