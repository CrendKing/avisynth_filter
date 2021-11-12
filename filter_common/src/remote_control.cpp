// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "remote_control.h"

#include "constants.h"
#include "filter.h"


namespace SynthFilter {

RemoteControl::RemoteControl(CSynthFilter &filter)
    : _filter(filter) {}

RemoteControl::~RemoteControl() {
    if (_hWnd != nullptr) {
        PostMessageW(_hWnd, WM_CLOSE, 0, 0);
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

auto RemoteControl::IsRunning() const -> bool {
    return _msgThread.joinable();
}

auto CALLBACK RemoteControl::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
    switch (uMsg) {
    case WM_COPYDATA: {
        const RemoteControl *rc = reinterpret_cast<const RemoteControl *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        return rc->HandleCopyData(reinterpret_cast<HWND>(wParam), reinterpret_cast<const COPYDATASTRUCT *>(lParam));
    }

    case WM_CLOSE: {
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

auto RemoteControl::Run() -> void {
#ifdef _DEBUG
    SetThreadDescription(GetCurrentThread(), L"CSynthFilter Remote Control");
#endif

    WNDCLASSA wc {};
    wc.lpfnWndProc = &RemoteControl::WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = API_WND_CLASS_NAME;
    if (RegisterClassA(&wc) == 0) {
        Environment::GetInstance().Log(L"Remote control failed to register window class: %5ld", GetLastError());
        return;
    }

    _hWnd = CreateWindowExA(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (_hWnd == nullptr) {
        Environment::GetInstance().Log(L"Remote control Failed to create window: %5ld", GetLastError());
        return;
    }
    SetWindowLongPtrW(_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    Environment::GetInstance().Log(L"Remote control started processing messages");

    MSG msg;
    BOOL msgRet;
    while ((msgRet = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (msgRet == -1) {
            Environment::GetInstance().Log(L"Remote control message loop error: %5ld", GetLastError());
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (!DestroyWindow(_hWnd)) {
        Environment::GetInstance().Log(L"Remote control failed to destroy window: %5ld", GetLastError());
        return;
    }

    if (!UnregisterClassA(API_WND_CLASS_NAME, wc.hInstance)) {
        Environment::GetInstance().Log(L"Remote control failed to unregister window class: %5ld", GetLastError());
        return;
    }

    Environment::GetInstance().Log(L"Remote control stopped");
}

auto RemoteControl::SendString(HWND hReceiverWindow, ULONG_PTR msgId, std::string_view data) const -> void {
    if (!hReceiverWindow) {
        return;
    }

    const COPYDATASTRUCT copyData { .dwData = msgId, .cbData = static_cast<DWORD>(data.size()), .lpData = const_cast<char *>(data.data()) };
    SendMessageTimeoutA(hReceiverWindow,
                        WM_COPYDATA,
                        reinterpret_cast<WPARAM>(_hWnd.load()),
                        reinterpret_cast<LPARAM>(&copyData),
                        SMTO_NORMAL | SMTO_ABORTIFHUNG,
                        REMOTE_CONTROL_SMTO_TIMEOUT_MS,
                        nullptr);
}

auto RemoteControl::SendString(HWND hReceiverWindow, ULONG_PTR msgId, std::wstring_view data) const -> void {
    // the API should always send UTF-8 strings
    // convert WCHAR to UTF-8

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
        SendString(hSenderWindow, copyData->dwData, JoinStrings(_filter.GetVideoFilterNames(), API_CSV_DELIMITER_STR));
        return TRUE;

    case API_MSG_GET_INPUT_WIDTH:
        return _filter.GetInputFormat().videoInfo.width;

    case API_MSG_GET_INPUT_HEIGHT:
        return _filter.GetInputFormat().videoInfo.height;

    case API_MSG_GET_INPUT_PAR:
        return static_cast<LRESULT>(llMulDiv(_filter.GetInputFormat().pixelAspectRatioNum, PAR_SCALE_FACTOR, _filter.GetInputFormat().pixelAspectRatioDen, 0));

    case API_MSG_GET_CURRENT_INPUT_FPS:
        return _filter.frameHandler->GetCurrentInputFrameRate();

    case API_MSG_GET_INPUT_SOURCE_PATH:
        SendString(hSenderWindow, copyData->dwData, _filter.GetVideoSourcePath().native());
        return TRUE;

    case API_MSG_GET_INPUT_CODEC:
        return _filter.GetInputFormat().GetCodecFourCC();

    case API_MSG_GET_INPUT_HDR_TYPE:
        return _filter.GetInputFormat().hdrType;

    case API_MSG_GET_INPUT_HDR_LUMINANCE:
        return _filter.GetInputFormat().hdrLuminance;

    case API_MSG_GET_SOURCE_AVG_FPS:
        return MainFrameServer::GetInstance().GetSourceAvgFrameRate();

    case API_MSG_GET_CURRENT_OUTPUT_FPS:
        return _filter.frameHandler->GetCurrentOutputFrameRate();

    case API_MSG_GET_AVS_STATE:
        return static_cast<LRESULT>(_filter.GetFrameServerState());

    case API_MSG_GET_AVS_ERROR:
        if (const std::optional<std::string> optFrameServerError = MainFrameServer::GetInstance().GetErrorString()) {
            SendString(hSenderWindow, copyData->dwData, *optFrameServerError);
            return TRUE;
        }

        return FALSE;

    case API_MSG_GET_AVS_SOURCE_FILE: {
        const std::filesystem::path &effectiveScriptPath = FrameServerCommon::GetInstance().GetScriptPath();
        if (effectiveScriptPath.empty()) {
            return FALSE;
        }

        SendString(hSenderWindow, copyData->dwData, effectiveScriptPath.native());
        return TRUE;
    }

    case API_MSG_SET_AVS_SOURCE_FILE: {
        const char *newScriptPathPtr = static_cast<const char *>(copyData->lpData);
        _filter.ReloadScript(std::filesystem::path(newScriptPathPtr, newScriptPathPtr + copyData->cbData));
        return TRUE;
    }

    default:
        return FALSE;
    }
}

}
