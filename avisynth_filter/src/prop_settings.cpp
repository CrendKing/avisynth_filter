// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "prop_settings.h"
#include "constants.h"


namespace AvsFilter {

CAvsFilterPropSettings::CAvsFilterPropSettings(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(SETTINGS_NAME_FULL, pUnk, IDD_SETTINGS_PAGE, IDS_SETTINGS) {
}

auto CAvsFilterPropSettings::OnConnect(IUnknown *pUnk) -> HRESULT {
    CheckPointer(pUnk, E_POINTER);
    return pUnk->QueryInterface(IID_IAvsFilter, reinterpret_cast<void **>(&_filter));
}

auto CAvsFilterPropSettings::OnDisconnect() -> HRESULT {
    if (_filter != nullptr) {
        _filter->Release();
        _filter = nullptr;
    }

    return S_OK;
}

auto CAvsFilterPropSettings::OnActivate() -> HRESULT {
    _configAvsPath = g_env.GetAvsPath();
    _avsFileManagedByRC = _configAvsPath != g_avs->GetScriptPath();
    if (_avsFileManagedByRC) {
        ShowWindow(GetDlgItem(m_Dlg, IDC_REMOTE_CONTROL_STATUS), SW_SHOW);
    }

    SetDlgItemTextW(m_Dlg, IDC_EDIT_AVS_FILE, _configAvsPath.c_str());

    EnableWindow(GetDlgItem(m_Dlg, IDC_BUTTON_RELOAD), !_avsFileManagedByRC && _filter->GetAvsState() != AvsState::Stopped);

    CheckDlgButton(m_Dlg, IDC_ENABLE_REMOTE_CONTROL, g_env.IsRemoteControlEnabled());

    for (const Format::PixelFormat &pixelFormat : Format::PIXEL_FORMATS) {
        CheckDlgButton(m_Dlg, pixelFormat.resourceId, g_env.IsInputFormatEnabled(pixelFormat.name));
    }

    const std::string title = std::string("<a>") + FILTER_NAME_BASE + " v" + FILTER_VERSION_STRING "</a>\nwith " + g_avs->GetVersionString();
    SetDlgItemTextA(m_hwnd, IDC_SYSLINK_TITLE, title.c_str());

    // move the focus to the tab of the settings page, effectively unfocus all controls in the page
    PostMessageW(m_hwnd, WM_NEXTDLGCTL, 1, FALSE);

    return S_OK;
}

auto CAvsFilterPropSettings::OnApplyChanges() -> HRESULT {
    if (!_configAvsPath.empty() && !std::filesystem::exists(_configAvsPath)) {
        MessageBoxW(m_hwnd, L"Configured AviSynth script file does not exist.", FILTER_NAME_FULL, MB_OK | MB_ICONWARNING);
    }
    g_env.SetAvsPath(_configAvsPath);

    g_env.SetRemoteControlEnabled(IsDlgButtonChecked(m_Dlg, IDC_ENABLE_REMOTE_CONTROL) == BST_CHECKED);

    for (const Format::PixelFormat &pixelFormat : Format::PIXEL_FORMATS) {
        g_env.SetInputFormatEnabled(pixelFormat.name, IsDlgButtonChecked(m_Dlg, pixelFormat.resourceId) == BST_CHECKED);
    }

    g_env.SaveSettings();

    if (_avsFileManagedByRC) {
        // TODO: put message in string table when going multi-language
        MessageBoxW(m_hwnd, L"AviSynth script file is currently managed by remote control. Your change if any is saved but not used.",
                    FILTER_NAME_FULL, MB_OK | MB_ICONINFORMATION);
    } else if (!_configAvsPath.empty()) {
        _filter->ReloadAvsFile(_configAvsPath);
    }

    return S_OK;
}

auto CAvsFilterPropSettings::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE) {
            if (const WORD eventTarget = LOWORD(wParam); eventTarget == IDC_EDIT_AVS_FILE) {
                std::array<WCHAR, STR_MAX_LENGTH> buffer;
                GetDlgItemTextW(hwnd, IDC_EDIT_AVS_FILE, buffer.data(), static_cast<int>(buffer.size()));

                if (const std::wstring newValue = std::wstring(buffer.data(), buffer.size()).c_str(); newValue != _configAvsPath) {
                    _configAvsPath = newValue;
                    SetDirty();
                }

                return 0;
            }
        } else if (HIWORD(wParam) == BN_CLICKED) {
            if (const WORD eventTarget = LOWORD(wParam); eventTarget == IDC_BUTTON_EDIT && !_configAvsPath.empty()) {
                ShellExecuteW(hwnd, L"edit", _configAvsPath.c_str(), nullptr, nullptr, SW_SHOW);
            } else if (eventTarget == IDC_BUTTON_RELOAD) {
                _filter->ReloadAvsFile(g_avs->GetScriptPath());
            } else if (eventTarget == IDC_BUTTON_BROWSE) {
                std::array<WCHAR, MAX_PATH> szFile {};

                OPENFILENAMEW ofn {};
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.lpstrFile = szFile.data();
                ofn.nMaxFile = static_cast<DWORD>(szFile.size());
                ofn.lpstrFilter = L"avs Files\0*.avs\0All Files\0*.*\0";
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileNameW(&ofn) == TRUE) {
                    SetDlgItemTextW(hwnd, IDC_EDIT_AVS_FILE, ofn.lpstrFile);
                    SetDirty();
                }
            } else if (eventTarget == IDC_ENABLE_REMOTE_CONTROL ||
                       (eventTarget > IDC_INPUT_FORMAT_START && eventTarget < IDC_INPUT_FORMAT_END)) {
                SetDirty();
            }

            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (GetWindowLongW(reinterpret_cast<HWND>(lParam), GWL_ID) == IDC_REMOTE_CONTROL_STATUS) {
            const HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            // make the color of the text control (IDC_TEXT_RC_CONTROLLING) blue to catch attention
            SetTextColor(hdc, RGB(0, 0, 0xff));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;

    case WM_NOTIFY: {
        if (const LPNMHDR notifyHeader = reinterpret_cast<LPNMHDR>(lParam); notifyHeader->idFrom == IDC_SYSLINK_TITLE) {
            switch (notifyHeader->code) {
            case NM_CLICK:
            case NM_RETURN:
                ShellExecuteW(hwnd, L"open", L"https://github.com/CrendKing/avisynth_filter", nullptr, nullptr, SW_SHOW);
                return 0;
            }
        }

        break;
    }
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

auto CAvsFilterPropSettings::SetDirty() -> void {
    m_bDirty = TRUE;
    if (m_pPageSite) {
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
    }
}

}
