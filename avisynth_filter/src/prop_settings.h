#pragma once

#include "pch.h"
#include "interfaces.h"


class CAvsFilterPropSettings : public CBasePropertyPage {
public:
    CAvsFilterPropSettings(LPUNKNOWN pUnk, HRESULT *phr);

private:
    auto OnConnect(IUnknown *pUnk) -> HRESULT override;
    auto OnDisconnect() -> HRESULT override;
    auto OnActivate() -> HRESULT override;
    auto OnApplyChanges() -> HRESULT override;
    auto OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR override;

    auto SetDirty() -> void;

    IAvsFilterSettings *_settings;
    std::string _avsFile;
};
