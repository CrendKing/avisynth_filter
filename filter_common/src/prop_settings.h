// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "filter.h"


namespace SynthFilter {

class
#ifdef AVSF_AVISYNTH
    __declspec(uuid("90c56868-7d47-4aa2-a42d-06406a6db35f"))
#else
    __declspec(uuid("22366882-6669-4403-a5d7-bfeb53ea88e5"))
#endif
        CSynthFilterPropSettings : public CBasePropertyPage {
public:
    CSynthFilterPropSettings(LPUNKNOWN pUnk, HRESULT *phr);

    DISABLE_COPYING(CSynthFilterPropSettings)

private:
    auto OnConnect(IUnknown *pUnk) -> HRESULT override;
    auto OnDisconnect() -> HRESULT override;
    auto OnActivate() -> HRESULT override;
    auto OnApplyChanges() -> HRESULT override;
    auto OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR override;

    auto SetDirty() -> void;

    CSynthFilter *_filter = nullptr;
    std::filesystem::path _configScriptPath;
    bool _scriptFileManagedByRC = false;
};

}
