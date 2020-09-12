#pragma once

#include "pch.h"
#include "interfaces.h"


namespace AvsFilter {

class RemoteControl {
public:
    RemoteControl(IAvsFilterStatus *status, IAvsFilterSettings *settings);
    virtual ~RemoteControl();

    auto Start() -> void;

private:
    static auto CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

    auto Run() -> void;
    auto SendString(HWND receiver, ULONG_PTR id, const std::string &data) const -> void;
    auto SendString(HWND receiver, ULONG_PTR id, const std::wstring &data) const -> void;
    auto HandleCopyData(HWND senderWnd, const COPYDATASTRUCT *copyData) const -> LRESULT;

    std::thread _msgThread;
    HWND _hWnd;

    IAvsFilterStatus *_status;
    IAvsFilterSettings *_settings;
};

}