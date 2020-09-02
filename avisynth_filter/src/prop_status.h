#pragma once

#include "pch.h"
#include "interfaces.h"


namespace AvsFilter {

class CAvsFilterPropStatus : public CBasePropertyPage {
public:
    CAvsFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr);

private:
    auto OnConnect(IUnknown *pUnk) -> HRESULT override;
    auto OnDisconnect() -> HRESULT override;
    auto OnActivate() -> HRESULT override;
    auto OnApplyChanges() -> HRESULT override;
    auto OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR override;

    IAvsFilterStatus *_status;
    bool _isSourcePathSet;
};

}