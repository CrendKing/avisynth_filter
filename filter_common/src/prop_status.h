// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "filter.h"


namespace SynthFilter {

class
#ifdef AVSF_AVISYNTH
__declspec(uuid("e58206ef-c9f2-4f8c-bf0b-975c28552700"))
#else
__declspec(uuid("4661947b-f51b-4b22-a023-482426611b1d"))
#endif
    CSynthFilterPropStatus : public CBasePropertyPage {
public:
    CSynthFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr);

    DISABLE_COPYING(CSynthFilterPropStatus)

private:
    auto OnConnect(IUnknown *pUnk) -> HRESULT override;
    auto OnDisconnect() -> HRESULT override;
    auto OnActivate() -> HRESULT override;
    auto OnApplyChanges() -> HRESULT override;
    auto OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR override;

    CSynthFilter *_filter = nullptr;
    bool _isSourcePathSet = false;
};

}
