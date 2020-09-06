#pragma once

#include "pch.h"
#include "interfaces.h"

#define REMOTE_CLASS_NAME L"PotPlayer64" //L"AVSFilterRemoteClass"
#define REMOTE_WINDOW_NAME L"AVSFilterRemoteWnd"

class RemoteControl {
public:
    RemoteControl(IAvsFilterStatus* status, IAvsFilterSettings* settings);
    virtual ~RemoteControl();

    auto Start() -> void;

private:
    static auto GetInstance(HWND wnd) -> RemoteControl*;
    auto SendData(HWND receiver, DWORD id, const std::wstring& data) -> void;

    static auto Run(LPVOID lpParam) -> DWORD;
    static auto WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT;

private:
    HANDLE _hThread;
    HWND _hWnd;

    IAvsFilterStatus* _status;
    IAvsFilterSettings* _settings;
};