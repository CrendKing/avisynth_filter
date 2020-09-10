
#include "pch.h"
#include "remote_control.h"
#include "remote_api.h"
#include "logging.h"

auto ConvertWideToUtf8(const std::wstring& wstr) -> std::string;
auto ConvertUtf8ToWide(const std::string& str) -> std::wstring;

RemoteControl::RemoteControl(IAvsFilterStatus* status, IAvsFilterSettings* settings):
	_status(status),
	_settings(settings),
	_hWnd(0),
	_hThread(0) {	
}

RemoteControl::~RemoteControl() {
	if (_hThread) {
		if(_hWnd)
			PostMessage(_hWnd, WM_CLOSE, 0, 0);

		WaitForSingleObject(_hThread, INFINITE);
		CloseHandle(_hThread);
	}
}

auto RemoteControl::Start() -> void {
	if (_hThread) return;

	_hThread = CreateThread(nullptr, 0, &RemoteControl::Run, this, 0, 0);
}

auto RemoteControl::Run(LPVOID lpParam) -> DWORD {
	RemoteControl* rc = static_cast<RemoteControl*>(lpParam);
   
    WNDCLASS wc;
    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = &RemoteControl::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = AVSF_CLASS_NAME;

    if (!RegisterClass(&wc))
        return 0;

	rc->_hWnd = CreateWindow(AVSF_CLASS_NAME, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, wc.hInstance, 0);
	if (!rc->_hWnd)
        return 0;

	SetWindowLongPtr(rc->_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(rc));

	Log("Remote control started");

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}	

	CloseHandle(rc->_hWnd);
	UnregisterClass(AVSF_CLASS_NAME, wc.hInstance);

	Log("Remote control stopped");

	return 0;
}

auto RemoteControl::GetInstance(HWND wnd) -> RemoteControl* {
	return reinterpret_cast<RemoteControl*>(GetWindowLongPtr(wnd, GWLP_USERDATA));
}

auto RemoteControl::SendData(HWND receiver, DWORD id, const std::wstring& data) -> void {
	SendData(receiver, id, ConvertWideToUtf8(data));
}

auto RemoteControl::SendData(HWND receiver, DWORD id, const std::string& data) -> void {
	COPYDATASTRUCT cd;
	cd.dwData = id;
	cd.lpData = const_cast<char*>(data.c_str());
	cd.cbData = static_cast<DWORD>(data.size());
	SendMessage(receiver, WM_COPYDATA, reinterpret_cast<WPARAM>(_hWnd), reinterpret_cast<LPARAM>(&cd));
}

auto RemoteControl::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
	RemoteControl* rc = RemoteControl::GetInstance(hWnd);
	switch (uMsg)
	{
	case AVSF_MSG:
		switch (wParam)
		{
		case AVSF_GET_API_VERSION:
			return AVSF_API_VERSION;

		case AVSF_GET_INPUT_WIDTH:
			return rc->_status->GetMediaInfo().videoInfo.width;
		case AVSF_GET_INPUT_HEIGHT:
			return rc->_status->GetMediaInfo().videoInfo.height;
		case AVSF_GET_INPUT_PAR:
			return static_cast<int>(std::round(rc->_status->GetMediaInfo().par * 1000));
		case AVSF_GET_INPUT_COLOR:
			return rc->_status->GetMediaInfo().GetCodec();
		case AVSF_GET_INPUT_FPS:
			return static_cast<int>(std::round(rc->_status->GetInputFrameRate() * 1000));
		case AVSF_GET_HDR_TYPE:
			return rc->_status->GetMediaInfo().hdr;
		case AVSF_GET_HDR_LUMINANCE:
			return rc->_status->GetMediaInfo().hdr_luminance;

		case AVSF_GET_PLAY_STATE:
			return static_cast<int>(rc->_status->GetPlayState());

		case AVSF_GET_FILENAME:
			rc->SendData(reinterpret_cast<HWND>(lParam), static_cast<DWORD>(wParam), rc->_status->GetSourcePath());
			break;
		case AVSF_GET_FILTERS: {
			std::wstring res;
			for (auto s : rc->_status->GetFiltersList())
				res += s + std::wstring(L";");
			rc->SendData(reinterpret_cast<HWND>(lParam), static_cast<DWORD>(wParam), res);
			} break;
		case AVSF_GET_ERROR:
			rc->SendData(reinterpret_cast<HWND>(lParam), static_cast<DWORD>(wParam), rc->_settings->GetAvsError());
			break;
		}
		return 0;

	case WM_COPYDATA: {
			COPYDATASTRUCT* cd = reinterpret_cast<COPYDATASTRUCT*>(lParam);
			if (cd->dwData == AVSF_SET_AVS_FILE)
			{
				std::wstring s = ConvertUtf8ToWide(std::string(static_cast<char*>(cd->lpData), cd->cbData));
				rc->_settings->SetAvsFile(s);
				rc->_settings->ReloadAvsFile();
			}
		}
		return 0;

	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}
