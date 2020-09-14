#include "pch.h"
#include "prop_settings.h"
#include "constants.h"


namespace AvsFilter {

CAvsFilterPropSettings::CAvsFilterPropSettings(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(NAME(SETTINGS_FULL), pUnk, IDD_SETTINGS_PAGE, IDS_SETTINGS)
    , _settings(nullptr)
    , _avsFileManagedByRC(false) {
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
    _avsFile = _settings->GetPrefAvsFile();
    _avsFileManagedByRC = _avsFile != _settings->GetEffectiveAvsFile();
    if (_avsFileManagedByRC) {
        ShowWindow(GetDlgItem(m_Dlg, IDC_TEXT_RC_CONTROLLING), SW_SHOW);
    }

    SetDlgItemText(m_Dlg, IDC_EDIT_AVS_FILE, _avsFile.c_str());

    const DWORD formatBits = _settings->GetInputFormats();
    for (int i = 0; i < sizeof(formatBits) * 8; ++i) {
        if ((formatBits & (1 << i)) != 0) {
            CheckDlgButton(m_Dlg, IDC_INPUT_FORMAT_NV12 + i, 1);
        }
    }

    return S_OK;
}

auto CAvsFilterPropSettings::OnApplyChanges() -> HRESULT {
    _settings->SetPrefAvsFile(_avsFile);

    DWORD formatBits = 0;
    for (int i = IDC_INPUT_FORMAT_NV12; i < IDC_INPUT_FORMAT_END; ++i) {
        if (IsDlgButtonChecked(m_Dlg, i) == BST_CHECKED) {
            formatBits |= 1 << (i - IDC_INPUT_FORMAT_NV12);
        }
    }
    _settings->SetInputFormats(formatBits);

    _settings->SaveSettings();

    if (_avsFileManagedByRC) {
        // TODO: put message in string table when going multi-language
        MessageBox(m_hwnd, L"AviSynth source file is currently managed by remote control. Your change is saved but not used.",
                   FILTER_NAME_WIDE, MB_OK | MB_ICONINFORMATION);
    } else {
        _settings->SetEffectiveAvsFile(_avsFile);
        _settings->ReloadAvsSource();
    }

    return S_OK;
}

auto CAvsFilterPropSettings::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE) {
            if (LOWORD(wParam) == IDC_EDIT_AVS_FILE) {
                wchar_t buf[STR_MAX_LENGTH];
                GetDlgItemText(hwnd, IDC_EDIT_AVS_FILE, buf, STR_MAX_LENGTH);
                const std::wstring newValue = std::wstring(buf, STR_MAX_LENGTH).c_str();

                if (newValue != _avsFile) {
                    _avsFile = newValue;
                    SetDirty();
                }

                return 0;
            }
        } else if (HIWORD(wParam) == BN_CLICKED) {
            if (LOWORD(wParam) == IDC_BUTTON_EDIT && !_avsFile.empty()) {
                ShellExecute(hwnd, L"edit", _avsFile.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
            } else if (LOWORD(wParam) == IDC_BUTTON_RELOAD) {
                _settings->ReloadAvsSource();
            } else if (LOWORD(wParam) == IDC_BUTTON_BROWSE) {
                wchar_t szFile[MAX_PATH] {};

                OPENFILENAME ofn {};
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = L"avs Files\0*.avs\0All Files\0*.*\0";
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileName(&ofn) == TRUE) {
                    SetDlgItemText(hwnd, IDC_EDIT_AVS_FILE, ofn.lpstrFile);
                }
            }

            SetDirty();
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
        // make the color of the text control (IDC_TEXT_RC_CONTROLLING) blue

        if (GetWindowLong(reinterpret_cast<HWND>(lParam), GWL_ID) == IDC_TEXT_RC_CONTROLLING) {
            const HDC hdc = reinterpret_cast<HDC>(wParam);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0xff));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

void CAvsFilterPropSettings::SetDirty() {
    m_bDirty = TRUE;
    if (m_pPageSite) {
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
    }
}

}