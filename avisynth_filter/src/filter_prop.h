#pragma once

#include "pch.h"
#include "settings.h"


class CAviSynthFilterProp : public CBasePropertyPage {
public:
    static auto WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown *;

    CAviSynthFilterProp(LPUNKNOWN pUnk, HRESULT *phr);

private:
    auto OnConnect(IUnknown *pUnk) -> HRESULT override;
    auto OnActivate() -> HRESULT override;
    auto OnApplyChanges() -> HRESULT override;
    auto OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR override;

    auto GetText() -> std::string;
    auto SetDirty() -> void;

    IAvsFilterSettings *_settings;
    std::string _avsFile;
    int _bufferBack;
    int _bufferAhead;
    std::unordered_set<int> _formatIndices;
};
