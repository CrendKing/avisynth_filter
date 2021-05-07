// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "api.h"
#include "util.h"


namespace SynthFilter {

class CSynthFilter;

class RemoteControl {
public:
    explicit RemoteControl(CSynthFilter &filter);
    ~RemoteControl();

    DISABLE_COPYING(RemoteControl)

    auto Start() -> void;
    auto Stop() -> void;
    auto IsRunning() const -> bool;

private:
    static auto CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

    auto Run() -> void;
    auto SendString(HWND hReceiverWindow, ULONG_PTR msgId, std::string_view data) const -> void;
    auto SendString(HWND hReceiverWindow, ULONG_PTR msgId, std::wstring_view data) const -> void;
    auto HandleCopyData(HWND hSenderWindow, const COPYDATASTRUCT *copyData) const -> LRESULT;

    static inline const std::wstring API_CSV_DELIMITER_STR = ConvertUtf8ToWide(API_CSV_DELIMITER);

    std::thread _msgThread;
    std::atomic<HWND> _hWnd = nullptr;

    CSynthFilter &_filter;
};

}
