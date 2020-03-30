#include "pch.h"
#include "prop_settings.h"
#include "constants.h"


CAvsFilterPropSettings::CAvsFilterPropSettings(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(NAME(SETTINGS_FULL), pUnk, IDD_PROPPAGE, IDS_SETTINGS)
    , _settings(nullptr)
    , _formatBits(0) {
}

auto CAvsFilterPropSettings::OnConnect(IUnknown *pUnk) -> HRESULT {
    CheckPointer(pUnk, E_POINTER);
    return pUnk->QueryInterface(IID_IAvsFilterSettings, reinterpret_cast<void **>(&_settings));
}

auto CAvsFilterPropSettings::OnDisconnect() -> HRESULT {
    if (_settings != nullptr) {
        _settings->Release();
        _settings = nullptr;
    }

    return S_OK;
}

auto CAvsFilterPropSettings::OnActivate() -> HRESULT {
    _avsFile = _settings->GetAvsFile();
    SetDlgItemText(m_Dlg, IDC_EDIT_AVS_FILE, _avsFile.c_str());

    _formatBits = _settings->GetInputFormats();
    for (int i = 0; i < sizeof(_formatBits) * 8; ++i) {
        if ((_formatBits & (1 << i)) != 0) {
            CheckDlgButton(m_Dlg, IDC_INPUT_FORMAT_NV12 + i, 1);
        }
    }

    return S_OK;
}

auto CAvsFilterPropSettings::OnApplyChanges() -> HRESULT {
    _settings->SetAvsFile(_avsFile);
    _settings->SetInputFormats(_formatBits);
    _settings->SaveSettings();

    return S_OK;
}

auto CAvsFilterPropSettings::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
        case WM_COMMAND: {
            if (HIWORD(wParam) == EN_CHANGE) {
                if (LOWORD(wParam) == IDC_EDIT_AVS_FILE) {
                    char buf[STR_MAX_LENGTH];
                    GetDlgItemText(hwnd, IDC_EDIT_AVS_FILE, buf, STR_MAX_LENGTH);
                    const std::string newValue = std::string(buf, STR_MAX_LENGTH).c_str();

                    if (newValue != _avsFile) {
                        _avsFile = newValue;
                        SetDirty();
                    }
                }
            } else if (HIWORD(wParam) == BN_CLICKED) {
                if (LOWORD(wParam) == IDC_BUTTON_EDIT && !_avsFile.empty()) {
                    ShellExecute(hwnd, "edit", _avsFile.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
                } else if (LOWORD(wParam) == IDC_BUTTON_RELOAD) {
                    _settings->SetReloadAvsFile(true);
                } else if (LOWORD(wParam) == IDC_BUTTON_BROWSE) {
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
                } else {
                    const int definition = LOWORD(wParam) - IDC_INPUT_FORMAT_NV12;
                    const bool buttonChecked = (IsDlgButtonChecked(hwnd, LOWORD(wParam)) == BST_CHECKED);

                    if (buttonChecked) {
                        _formatBits |= 1 << definition;
                    } else {
                        _formatBits &= ~(1 << definition);
                    }

                    SetDirty();
                }
            }

            break;
        }
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

void CAvsFilterPropSettings::SetDirty() {
    m_bDirty = TRUE;
    if (m_pPageSite) {
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
    }
}