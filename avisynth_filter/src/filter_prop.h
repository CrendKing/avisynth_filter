#pragma once

#include "pch.h"
#include "avs_file.h"


class CAviSynthFilterProp : public CBasePropertyPage {
public:
    static auto WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown *;

    CAviSynthFilterProp(LPUNKNOWN pUnk, HRESULT *phr);

private:
    auto OnConnect(IUnknown *pUnk) -> HRESULT override;
    auto OnDisconnect() -> HRESULT override;
    auto OnActivate() -> HRESULT override;
    auto OnApplyChanges() -> HRESULT override;
    auto OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR override;

    auto GetText() -> std::string;

    IAvsFile *_avsFileInterface;
    std::string _avsFileValue;
};