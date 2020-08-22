
#include "pch.h"
#include "remote_control.h"
#include "logging.h"

auto ConvertWideToUtf8(const std::wstring& wstr) -> std::string;
auto ConvertUtf8ToWide(const std::string& str) -> std::wstring;

RemoteControl::RemoteControl(IAvsFilterStatus* status, IAvsFilterSettings* settings):
	_status(status),
	_settings(settings),
	_hWnd(0) {
	_hThread = CreateThread(NULL, 0, &RemoteControl::Run, this, 0, 0);
}

RemoteControl::~RemoteControl() {
	if (_hThread) {
		if(_hWnd)
			PostMessage(_hWnd, WM_CLOSE, 0, 0);

		WaitForSingleObject(_hThread, INFINITE);
		CloseHandle(_hThread);
	}
}

auto RemoteControl::Run(LPVOID lpParam) -> DWORD {
	RemoteControl* rc = static_cast<RemoteControl*>(lpParam);
   
    WNDCLASS wc;
    memset(&wc, 0, sizeof(WNDCLASS));
    wc.lpfnWndProc = &RemoteControl::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = REMOTE_CLASS_NAME;

    if (!RegisterClass(&wc))
        return 0;

	rc->_hWnd = CreateWindow(REMOTE_CLASS_NAME, REMOTE_WINDOW_NAME, 0, 0, 0, 0, 0, 0, 0, wc.hInstance, 0);
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
	UnregisterClass(REMOTE_CLASS_NAME, wc.hInstance);

	Log("Remote control stopped");

	return 0;
}

auto RemoteControl::GetInstance(HWND wnd) -> RemoteControl* {
	return reinterpret_cast<RemoteControl*>(GetWindowLongPtr(wnd, GWLP_USERDATA));
}

//TODO: change me!
#define POT_COMMAND    WM_USER
#define POT_GET_PLAY_STATUS  0x5006 // 0:Stopped, 1:Paused, 2:Running
#define POT_GET_AVISYNTH_USE  0x6000
#define POT_SET_AVISYNTH_USE  0x6001 // 0: off, 1:on
#define POT_GET_VIDEO_WIDTH   0x6030
#define POT_GET_VIDEO_HEIGHT  0x6031
#define POT_GET_VIDEO_FPS   0x6032 // scale by 1000
#define POT_GET_PLAYFILE_NAME  0x6020
#define POT_GET_AVISYNTH_SCRIPT  0x6002
#define POT_SET_AVISYNTH_SCRIPT  0x6003

auto RemoteControl::SendData(HWND receiver, DWORD id, const std::wstring& data) -> void {
	COPYDATASTRUCT cd;
	cd.dwData = id;
	std::string utf8data = ConvertWideToUtf8(data);
	cd.lpData = const_cast<char*>(utf8data.c_str());
	cd.cbData = static_cast<DWORD>(utf8data.size());
	SendMessage(receiver, WM_COPYDATA, reinterpret_cast<WPARAM>(_hWnd), reinterpret_cast<LPARAM>(&cd));
}

auto RemoteControl::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
	RemoteControl* rc = RemoteControl::GetInstance(hWnd);
	switch (uMsg)
	{
	case POT_COMMAND:
		switch (wParam)
		{
		case POT_GET_VIDEO_WIDTH: 
			return rc->_status->GetFrameNumbers().first;

		case POT_GET_VIDEO_HEIGHT:
			return rc->_status->GetFrameNumbers().second;
			
		case POT_GET_VIDEO_FPS:
			return static_cast<int>(floor(rc->_status->GetInputFrameRate() * 1000));

		case POT_GET_PLAY_STATUS:
			return 2; //TODO

		case POT_GET_PLAYFILE_NAME:
			rc->SendData(reinterpret_cast<HWND>(lParam), static_cast<DWORD>(wParam), rc->_status->GetSourcePath());
			break;

		case POT_SET_AVISYNTH_USE: 
			//TODO
			return 0;
		}
		return 0;

	case WM_COPYDATA: {
			COPYDATASTRUCT* cd = reinterpret_cast<COPYDATASTRUCT*>(lParam);
			if (cd->dwData == POT_SET_AVISYNTH_SCRIPT)
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
