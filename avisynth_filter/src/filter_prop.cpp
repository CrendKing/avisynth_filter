#include "pch.h"
#include "filter_prop.h"
#include "constants.h"


auto WINAPI CAviSynthFilterProp::CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    CAviSynthFilterProp *newProp = new CAviSynthFilterProp(pUnk, phr);

    if (newProp == nullptr && phr != nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newProp;
}

CAviSynthFilterProp::CAviSynthFilterProp(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(PROPERTY_PAGE_NAME, pUnk, IDD_PROPPAGE, IDS_TITLE)
    , _settings(nullptr) {
}

auto CAviSynthFilterProp::OnConnect(IUnknown *pUnk) -> HRESULT {
    CheckPointer(pUnk, E_POINTER);
    return pUnk->QueryInterface(IID_IAvsFilterSettings, reinterpret_cast<void **>(&_settings));
}

auto CAviSynthFilterProp::OnActivate() -> HRESULT {
    _avsFile = _settings->GetAvsFile();
    SetDlgItemText(m_Dlg, IDC_EDIT_AVS_FILE, _avsFile.c_str());

    _bufferBack = _settings->GetBufferBack();
    _bufferAhead = _settings->GetBufferAhead();
    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_BACK, UDM_SETPOS32, 0, _bufferBack);
    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_AHEAD, UDM_SETPOS32, 0, _bufferAhead);

    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_BACK, UDM_SETRANGE32, BUFFER_FRAMES_MIN, BUFFER_FRAMES_MAX);
    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_BACK, UDM_SETRANGE32, BUFFER_FRAMES_MIN, BUFFER_FRAMES_MAX);
    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_AHEAD, UDM_SETRANGE32, BUFFER_FRAMES_MIN, BUFFER_FRAMES_MAX);
    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_AHEAD, UDM_SETRANGE32, BUFFER_FRAMES_MIN, BUFFER_FRAMES_MAX);

    UDACCEL accels = { 0, 1 };
    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_BACK, UDM_SETACCEL, 1, reinterpret_cast<LPARAM>(&accels));
    SendDlgItemMessage(m_Dlg, IDC_SPIN_BUFFER_AHEAD, UDM_SETACCEL, 1, reinterpret_cast<LPARAM>(&accels));

    _formatIndices = _settings->GetFormats();
    for (const int formatIndex : _formatIndices) {
        CheckDlgButton(m_Dlg, IDC_FORMAT_NV12 + formatIndex, 1);
    }

    return S_OK;
}

auto CAviSynthFilterProp::OnApplyChanges() -> HRESULT {
    _settings->SetAvsFile(_avsFile);
    _settings->SetBufferBack(_bufferBack);
    _settings->SetBufferAhead(_bufferAhead);
    _settings->SetFormats(_formatIndices);
    _settings->SaveSettings();

    return S_OK;
}

auto CAviSynthFilterProp::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
    case WM_COMMAND:
    {
        if (HIWORD(wParam) == EN_CHANGE) {
            if (LOWORD(wParam) == IDC_EDIT_AVS_FILE) {
                char buf[MAX_PATH];
                const UINT len = GetDlgItemText(m_Dlg, IDC_EDIT_AVS_FILE, buf, MAX_PATH);
                const std::string newValue = std::string(buf, len).c_str();

                if (newValue != _avsFile) {
                    _avsFile = newValue;
                    SetDirty();
                }
            } else if (LOWORD(wParam) == IDC_EDIT_BUFFER_BACK || LOWORD(wParam) == IDC_EDIT_BUFFER_AHEAD) {
                int newValue = GetDlgItemInt(hwnd, LOWORD(wParam), nullptr, FALSE);
                if (newValue < BUFFER_FRAMES_MIN || newValue > BUFFER_FRAMES_MAX) {
                    SendDlgItemMessage(m_Dlg, LOWORD(wParam), UDM_SETPOS32, 0, newValue);
                } else {
                    if (LOWORD(wParam) == IDC_EDIT_BUFFER_BACK && _bufferBack != newValue) {
                        _bufferBack = newValue;
                        SetDirty();
                    } else if (LOWORD(wParam) == IDC_EDIT_BUFFER_AHEAD && _bufferAhead != newValue) {
                        _bufferAhead = newValue;
                        SetDirty();
                    }
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
                const int formatIndex = LOWORD(wParam) - IDC_FORMAT_NV12;
                const bool buttonChecked = (IsDlgButtonChecked(hwnd, LOWORD(wParam)) == BST_CHECKED);

                if (buttonChecked) {
                    _formatIndices.emplace(formatIndex);
                } else {
                    _formatIndices.erase(formatIndex);
                }

                SetDirty();
            }
        }

        break;
    }
    case WM_NOTIFY:
    {
        LPNMUPDOWN upDown = reinterpret_cast<LPNMUPDOWN>(lParam);
        if (upDown->hdr.code == UDN_DELTAPOS) {
            int newValue = upDown->iPos + upDown->iDelta;
            if (newValue >= BUFFER_FRAMES_MIN && newValue <= BUFFER_FRAMES_MAX) {
                if (upDown->hdr.idFrom == IDC_SPIN_BUFFER_BACK && _bufferBack != newValue) {
                    _bufferBack = newValue;
                    SetDirty();
                } else if (upDown->hdr.idFrom == IDC_SPIN_BUFFER_AHEAD && _bufferAhead != newValue) {
                    _bufferAhead = newValue;
                    SetDirty();
                }
            }
        }
    }
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

void CAviSynthFilterProp::SetDirty() {
    m_bDirty = TRUE;
    if (m_pPageSite) {
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
    }
}