// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"
#include "filter.h"


namespace AvsFilter {

class CAvsFilterPropStatus : public CBasePropertyPage {
public:
    CAvsFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr);

    DISABLE_COPYING(CAvsFilterPropStatus)

private:
    auto OnConnect(IUnknown *pUnk) -> HRESULT override;
    auto OnDisconnect() -> HRESULT override;
    auto OnActivate() -> HRESULT override;
    auto OnApplyChanges() -> HRESULT override;
    auto OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR override;

    CAviSynthFilter *_filter = nullptr;
    bool _isSourcePathSet = false;
};

}
