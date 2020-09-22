#include "pch.h"
#include "remote_control.h"
#include "api.h"
#include "constants.h"
#include "logging.h"
#include "util.h"


namespace AvsFilter {
	
RemoteControl::RemoteControl(IAvsFilterStatus *status, IAvsFilterSettings *settings)
	: _hWnd(nullptr)
	, _status(status)
	, _settings(settings) {
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
		const RemoteControl *rc = reinterpret_cast<const RemoteControl *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
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
	WNDCLASS wc {};
	wc.lpfnWndProc = &RemoteControl::WndProc;
	wc.hInstance = g_hInst;
	wc.lpszClassName = API_CLASS_NAME;
	if (!RegisterClass(&wc)) {
		return;
	}

	_hWnd = CreateWindowEx(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
	if (!_hWnd) {
		return;
	}
	SetWindowLongPtr(_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	Log("Remote control started");

	MSG msg;
	BOOL msgRet;
	while ((msgRet = GetMessage(&msg, nullptr, 0, 0)) != 0) {
		if (msgRet == -1) {
			Log("Remote control message loop error: %5i", GetLastError());
			break;
		} else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	DestroyWindow(_hWnd);
	UnregisterClass(API_CLASS_NAME, wc.hInstance);

	Log("Remote control stopped");
}

auto RemoteControl::SendString(HWND hReceiverWindow, ULONG_PTR msgId, const std::string &data) const -> void {
	if (!hReceiverWindow) {
		return;
	}
	
	const COPYDATASTRUCT copyData { msgId, static_cast<DWORD>(data.size()), const_cast<char *>(data.c_str()) };
	SendMessageTimeout(hReceiverWindow, WM_COPYDATA, reinterpret_cast<WPARAM>(_hWnd), reinterpret_cast<LPARAM>(&copyData),
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
		SendString(hSenderWindow, copyData->dwData, JoinStrings(_status->GetVideoFilterNames(), API_CSV_DELIMITER));
		return TRUE;

	case API_MSG_GET_INPUT_WIDTH:
		return _status->GetInputMediaInfo().videoInfo.width;

	case API_MSG_GET_INPUT_HEIGHT:
		return _status->GetInputMediaInfo().videoInfo.height;

	case API_MSG_GET_INPUT_PAR:
		return _status->GetInputMediaInfo().pixelAspectRatio;

	case API_MSG_GET_CURRENT_INPUT_FPS:
		return _status->GetCurrentInputFrameRate();

	case API_MSG_GET_INPUT_SOURCE_PATH:
		SendString(hSenderWindow, copyData->dwData, _status->GetVideoSourcePath());
		return TRUE;

	case API_MSG_GET_INPUT_CODEC:
		return _status->GetInputMediaInfo().GetCodecFourCC();

	case API_MSG_GET_INPUT_HDR_TYPE:
		return _status->GetInputMediaInfo().hdrType;
		
	case API_MSG_GET_INPUT_HDR_LUMINANCE:
		return _status->GetInputMediaInfo().hdrLuminance;

	case API_MSG_GET_SOURCE_AVG_FPS:
		return _status->GetSourceAvgFrameRate();

	case API_MSG_GET_CURRENT_OUTPUT_FPS:
		return _status->GetCurrentOutputFrameRate();

	case API_MSG_GET_AVS_STATE:
		return static_cast<LRESULT>(_status->GetAvsState());

	case API_MSG_GET_AVS_ERROR:
		if (auto avsError = _status->GetAvsError()) {
			SendString(hSenderWindow, copyData->dwData, *avsError);
			return TRUE;
		} else {
			return FALSE;
		}

	case API_MSG_GET_AVS_SOURCE_FILE: {
		const std::wstring effectiveAvsFile = _settings->GetEffectiveAvsFile();
		if (effectiveAvsFile.empty()) {
			return FALSE;
		}

		SendString(hSenderWindow, copyData->dwData, effectiveAvsFile);
		return TRUE;
	}

	case API_MSG_SET_AVS_SOURCE_FILE: {
		const std::wstring newAvsFile = ConvertUtf8ToWide(std::string(static_cast<const char *>(copyData->lpData), copyData->cbData));
		_settings->SetEffectiveAvsFile(newAvsFile);
		_settings->ReloadAvsSource();
		return TRUE;
	}

	default:
		return FALSE;
	}
}

}