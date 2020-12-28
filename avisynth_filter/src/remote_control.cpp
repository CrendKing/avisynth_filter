// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "remote_control.h"
#include "api.h"
#include "avs_handler.h"
#include "constants.h"
#include "environment.h"
#include "filter.h"
#include "util.h"


namespace AvsFilter {

RemoteControl::RemoteControl(CAviSynthFilter &filter)
    : _hWnd(nullptr)
    , _filter(filter) {
}

RemoteControl::~RemoteControl() {
    if (_hWnd) {
        PostMessage(_hWnd, WM_CLOSE, 0, 0);
    }

    if (_msgThread.joinable()) {
        _msgThread.join();
    }
}

auto RemoteControl::Start() -> void {
    if (!_msgThread.joinable()) {
        _msgThread = std::thread(&RemoteControl::Run, this);
    }
}

auto CALLBACK RemoteControl::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
    switch (uMsg) {
    case WM_COPYDATA: {
        const RemoteControl *rc = reinterpret_cast<const RemoteControl *>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
        return rc->HandleCopyData(reinterpret_cast<HWND>(wParam), reinterpret_cast<const COPYDATASTRUCT *>(lParam));
    }

    case WM_CLOSE: {
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

auto RemoteControl::Run() -> void {
	WNDCLASSA wc {};
	wc.lpfnWndProc = &RemoteControl::WndProc;
	wc.hInstance = g_hInst;
	wc.lpszClassName = API_WND_CLASS_NAME;
	if (!RegisterClassA(&wc)) {
		return;
	}

	_hWnd = CreateWindowExA(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
	if (!_hWnd) {
		return;
	}
	SetWindowLongPtrA(_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	g_env.Log("Remote control started");

	MSG msg;
	BOOL msgRet;
	while ((msgRet = GetMessageA(&msg, nullptr, 0, 0)) != 0) {
		if (msgRet == -1) {
			g_env.Log("Remote control message loop error: %5lu", GetLastError());
			break;
		}

		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

	DestroyWindow(_hWnd);
	UnregisterClassA(API_WND_CLASS_NAME, wc.hInstance);

	g_env.Log("Remote control stopped");
}

auto RemoteControl::SendString(HWND hReceiverWindow, ULONG_PTR msgId, const std::string &data) const -> void {
	if (!hReceiverWindow) {
		return;
	}

	const COPYDATASTRUCT copyData { .dwData = msgId, .cbData = static_cast<DWORD>(data.size()), .lpData = const_cast<char *>(data.c_str()) };
	SendMessageTimeoutA(hReceiverWindow, WM_COPYDATA, reinterpret_cast<WPARAM>(_hWnd), reinterpret_cast<LPARAM>(&copyData),
					    SMTO_NORMAL | SMTO_ABORTIFHUNG, REMOTE_CONTROL_SMTO_TIMEOUT_MS, nullptr);
}

auto RemoteControl::SendString(HWND hReceiverWindow, ULONG_PTR msgId, const std::wstring &data) const -> void {
    std::string utf8Data;
    if (!data.empty()) {
        utf8Data = ConvertWideToUtf8(data);
    }

    SendString(hReceiverWindow, msgId, utf8Data);
}

auto RemoteControl::HandleCopyData(HWND hSenderWindow, const COPYDATASTRUCT *copyData) const -> LRESULT {
    if (copyData == nullptr) {
        return FALSE;
    }

    switch (copyData->dwData) {
    case API_MSG_GET_API_VERSION:
        return API_VERSION;

    case API_MSG_GET_VIDEO_FILTERS:
        SendString(hSenderWindow, copyData->dwData, JoinStrings(_filter.GetVideoFilterNames(), API_CSV_DELIMITER));
        return TRUE;

    case API_MSG_GET_INPUT_WIDTH:
        return _filter.GetInputFormat().videoInfo.width;

    case API_MSG_GET_INPUT_HEIGHT:
        return _filter.GetInputFormat().videoInfo.height;

    case API_MSG_GET_INPUT_PAR:
        return _filter.GetInputFormat().pixelAspectRatio;

    case API_MSG_GET_CURRENT_INPUT_FPS:
        return _filter.frameHandler.GetCurrentInputFrameRate();

    case API_MSG_GET_INPUT_SOURCE_PATH:
        SendString(hSenderWindow, copyData->dwData, _filter.GetVideoSourcePath());
        return TRUE;

    case API_MSG_GET_INPUT_CODEC:
        return _filter.GetInputFormat().GetCodecFourCC();

    case API_MSG_GET_INPUT_HDR_TYPE:
        return _filter.GetInputFormat().hdrType;

    case API_MSG_GET_INPUT_HDR_LUMINANCE:
        return _filter.GetInputFormat().hdrLuminance;

    case API_MSG_GET_SOURCE_AVG_FPS:
        return g_avs->GetSourceAvgFrameRate();

    case API_MSG_GET_CURRENT_OUTPUT_FPS:
        return _filter.frameHandler.GetCurrentOutputFrameRate();

    case API_MSG_GET_AVS_STATE:
        return static_cast<LRESULT>(_filter.GetAvsState());

    case API_MSG_GET_AVS_ERROR:
        if (const std::optional<std::string> optAvsError = g_avs->GetErrorString()) {
            SendString(hSenderWindow, copyData->dwData, *optAvsError);
            return TRUE;
        }

        return FALSE;

    case API_MSG_GET_AVS_SOURCE_FILE: {
        const std::wstring effectiveAvsFile = g_avs->GetScriptFile();
        if (effectiveAvsFile.empty()) {
            return FALSE;
        }

        SendString(hSenderWindow, copyData->dwData, effectiveAvsFile);
        return TRUE;
    }

    case API_MSG_SET_AVS_SOURCE_FILE: {
        const std::wstring newAvsFile = ConvertUtf8ToWide(std::string(static_cast<const char *>(copyData->lpData), copyData->cbData));
        _filter.ReloadAvsFile(newAvsFile);
        return TRUE;
    }

    default:
        return FALSE;
    }
}

}
