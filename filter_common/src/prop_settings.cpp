// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "prop_settings.h"
#include "constants.h"


namespace SynthFilter {

CSynthFilterPropSettings::CSynthFilterPropSettings(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(SETTINGS_NAME_FULL, pUnk, IDD_SETTINGS_PAGE, IDS_SETTINGS) {
}

auto CSynthFilterPropSettings::OnConnect(IUnknown *pUnk) -> HRESULT {
    CheckPointer(pUnk, E_POINTER);
    _filter = reinterpret_cast<CSynthFilter *>(pUnk);
    _filter->AddRef();

    return S_OK;
}

auto CSynthFilterPropSettings::OnDisconnect() -> HRESULT {
    if (_filter != nullptr) {
        _filter->Release();
        _filter = nullptr;
    }

    return S_OK;
}

auto CSynthFilterPropSettings::OnActivate() -> HRESULT {
    _configScriptPath = Environment::GetInstance().GetScriptPath();
    _scriptFileManagedByRC = _configScriptPath != FrameServerCommon::GetInstance().GetScriptPath();
    if (_scriptFileManagedByRC) {
        ShowWindow(GetDlgItem(m_Dlg, IDC_REMOTE_CONTROL_STATUS), SW_SHOW);
    }

    SetDlgItemTextW(m_Dlg, IDC_EDIT_SCRIPT_FILE, _configScriptPath.c_str());

    EnableWindow(GetDlgItem(m_Dlg, IDC_BUTTON_RELOAD), !_scriptFileManagedByRC && _filter->GetFrameServerState() != AvsState::Stopped);

    CheckDlgButton(m_Dlg, IDC_ENABLE_REMOTE_CONTROL, Environment::GetInstance().IsRemoteControlEnabled());

    std::ranges::for_each(Format::PIXEL_FORMATS, [this](const Format::PixelFormat &pixelFormat) {
        CheckDlgButton(m_Dlg, pixelFormat.resourceId, Environment::GetInstance().IsInputFormatEnabled(pixelFormat.name));
    });

#ifdef AVSF_VAPOURSYNTH
    EnableWindow(GetDlgItem(m_Dlg, IDC_INPUT_FORMAT_RGB24), FALSE);
#endif

    const std::string title = std::string("<a>") + FILTER_NAME_BASE + " v" + FILTER_VERSION_STRING "</a>\nwith " + FrameServerCommon::GetInstance().GetVersionString();
    SetDlgItemTextA(m_hwnd, IDC_SYSLINK_TITLE, title.c_str());

    // move the focus to the tab of the settings page, effectively unfocus all controls in the page
    PostMessageW(m_hwnd, WM_NEXTDLGCTL, 1, FALSE);

    return S_OK;
}

auto CSynthFilterPropSettings::OnApplyChanges() -> HRESULT {
    if (!_configScriptPath.empty() && !std::filesystem::exists(_configScriptPath)) {
        MessageBoxW(m_hwnd, L"Configured script file does not exist.", FILTER_NAME_FULL, MB_OK | MB_ICONWARNING);
    }
    Environment::GetInstance().SetScriptPath(_configScriptPath);

    Environment::GetInstance().SetRemoteControlEnabled(IsDlgButtonChecked(m_Dlg, IDC_ENABLE_REMOTE_CONTROL) == BST_CHECKED);

    std::ranges::for_each(Format::PIXEL_FORMATS, [this](const Format::PixelFormat &pixelFormat) {
        Environment::GetInstance().SetInputFormatEnabled(pixelFormat.name, IsDlgButtonChecked(m_Dlg, pixelFormat.resourceId) == BST_CHECKED);
    });

    Environment::GetInstance().SaveSettings();

    if (_scriptFileManagedByRC) {
        // TODO: put message in string table when going multi-language
        MessageBoxW(m_hwnd, L"The script file is currently managed by remote control. Your change if any is saved but not used.",
                    FILTER_NAME_FULL, MB_OK | MB_ICONINFORMATION);
    } else if (!_configScriptPath.empty()) {
        _filter->ReloadScript(_configScriptPath);
    }

    return S_OK;
}

auto CSynthFilterPropSettings::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE) {
            if (const WORD eventTarget = LOWORD(wParam); eventTarget == IDC_EDIT_SCRIPT_FILE) {
                std::array<WCHAR, STR_MAX_LENGTH> buffer;
                GetDlgItemTextW(hwnd, IDC_EDIT_SCRIPT_FILE, buffer.data(), static_cast<int>(buffer.size()));

                if (const std::wstring newValue = std::wstring(buffer.data(), buffer.size()).c_str(); newValue != _configScriptPath) {
                    _configScriptPath = newValue;
                    SetDirty();
                }

                return 0;
            }
        } else if (HIWORD(wParam) == BN_CLICKED) {
            if (const WORD eventTarget = LOWORD(wParam); eventTarget == IDC_BUTTON_EDIT && !_configScriptPath.empty()) {
                ShellExecuteW(hwnd, L"edit", _configScriptPath.c_str(), nullptr, nullptr, SW_SHOW);
            } else if (eventTarget == IDC_BUTTON_RELOAD) {
                _filter->ReloadScript(FrameServerCommon::GetInstance().GetScriptPath());
            } else if (eventTarget == IDC_BUTTON_BROWSE) {
                std::array<WCHAR, MAX_PATH> szFile = {};

                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.lpstrFile = szFile.data();
                ofn.nMaxFile = static_cast<DWORD>(szFile.size());
#ifdef AVSF_AVISYNTH
                ofn.lpstrFilter = L"avs Files\0*.avs\0All Files\0*.*\0";
#else
                ofn.lpstrFilter = L"vpy Files\0*.vpy\0All Files\0*.*\0";
#endif
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileNameW(&ofn) == TRUE) {
                    SetDlgItemTextW(hwnd, IDC_EDIT_SCRIPT_FILE, ofn.lpstrFile);
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

auto CSynthFilterPropSettings::SetDirty() -> void {
    m_bDirty = TRUE;
    if (m_pPageSite) {
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
    }
}

}
