#include "pch.h"
#include "filter_prop.h"
#include "g_constants.h"


auto WINAPI CAviSynthFilterProp::CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    CAviSynthFilterProp *newProp = new CAviSynthFilterProp(pUnk, phr);

    if (newProp == nullptr && phr != nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newProp;
}

CAviSynthFilterProp::CAviSynthFilterProp(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(PROPERTY_PAGE_NAME, pUnk, IDD_PROPPAGE, IDS_TITLE)
    , _avsFileInterface(nullptr) {
}

auto CAviSynthFilterProp::OnConnect(IUnknown *pUnk) -> HRESULT {
    if (pUnk == nullptr) {
        return E_POINTER;
    }

    ASSERT(_avsFileInterface == nullptr);
    return pUnk->QueryInterface(IID_IAvsFile, reinterpret_cast<void **>(&_avsFileInterface));
}

auto CAviSynthFilterProp::OnDisconnect() -> HRESULT {
    if (_avsFileInterface != nullptr) {
        _avsFileInterface->Release();
        _avsFileInterface = nullptr;
    }
    return S_OK;
}

auto CAviSynthFilterProp::OnActivate() -> HRESULT {
    ASSERT(_avsFileInterface != nullptr);

    HRESULT hr = _avsFileInterface->GetAvsFile(_avsFileValue);
    if (SUCCEEDED(hr)) {
        SetDlgItemText(m_Dlg, IDC_EDIT_AVS_FILE, _avsFileValue.c_str());
    }

    return hr;
}

auto CAviSynthFilterProp::OnApplyChanges() -> HRESULT {
    _avsFileValue = GetText();
    _avsFileInterface->UpdateAvsFile(_avsFileValue);
    _avsFileInterface->ReloadAvsFile();
    return S_OK;
}

auto CAviSynthFilterProp::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_EDIT_AVS_FILE && HIWORD(wParam) == EN_CHANGE) {
            if (GetText() != _avsFileValue) {
                m_bDirty = TRUE;
                if (m_pPageSite) {
                    m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                }
            }
        } else if (LOWORD(wParam) == IDC_BUTTON_BROWSE && HIWORD(wParam) == BN_CLICKED) {
            char szFile[MAX_PATH] {};

            OPENFILENAME ofn {};
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = "avs Files\0*.avs\0All Files\0*.*\0";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileName(&ofn) == TRUE) {
                SetDlgItemText(hwnd, IDC_EDIT_AVS_FILE, ofn.lpstrFile);
            }
        } else if (LOWORD(wParam) == IDC_BUTTON_RELOAD && HIWORD(wParam) == BN_CLICKED) {
            _avsFileInterface->ReloadAvsFile();
        }

        break;
    }
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

auto CAviSynthFilterProp::GetText() -> std::string {
    std::string ret;

    char buf[MAX_PATH];
    const UINT len = GetDlgItemText(m_Dlg, IDC_EDIT_AVS_FILE, buf, MAX_PATH);
    ret = std::string(buf, len).c_str();

    return ret;
}