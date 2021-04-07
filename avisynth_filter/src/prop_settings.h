// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "filter.h"


namespace AvsFilter {

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

    CAviSynthFilter *_filter = nullptr;
    std::filesystem::path _configAvsPath;
    bool _avsFileManagedByRC = false;
};

}
