#include "pch.h"
#include "prop_status.h"
#include "constants.h"


CAvsFilterPropStatus::CAvsFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(NAME(STATUS_FULL), pUnk, IDD_STATUSPAGE, IDS_STATUS)
    , _status(nullptr) {
}

auto CAvsFilterPropStatus::OnConnect(IUnknown *pUnk) -> HRESULT {
    CheckPointer(pUnk, E_POINTER);
    return pUnk->QueryInterface(IID_IAvsFilterStatus, reinterpret_cast<void **>(&_status));
}

auto CAvsFilterPropStatus::OnDisconnect() -> HRESULT {
    if (_status != nullptr) {
        _status->Release();
        _status = nullptr;
    }

    return S_OK;
}

auto CAvsFilterPropStatus::OnActivate() -> HRESULT {
    if (SetTimer(m_hwnd, IDT_TIMER_STATUS, 500, nullptr) == 0) {
        return E_FAIL;
    }

    return S_OK;
}

auto CAvsFilterPropStatus::OnApplyChanges() -> HRESULT {
    if (!KillTimer(m_hwnd, IDT_TIMER_STATUS)) {
        return E_FAIL;
    }

    return S_OK;
}

auto CAvsFilterPropStatus::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
        case WM_COMMAND: {
            if (HIWORD(wParam) == EN_SETFOCUS) {
                HideCaret(reinterpret_cast<HWND>(lParam));
            }
            break;
        } case WM_TIMER: {
            SetDlgItemText(hwnd, IDC_EDIT_BUFFER_SIZE, std::to_string(_status->GetBufferSize()).c_str());
            SetDlgItemText(hwnd, IDC_EDIT_BUFFER_AHEAD, std::to_string(_status->GetBufferAhead()).c_str());
            SetDlgItemText(hwnd, IDC_EDIT_BUFFER_AHEAD_OVERTIME, std::to_string(_status->GetBufferAheadOvertime()).c_str());
            SetDlgItemText(hwnd, IDC_EDIT_BUFFER_BACK, std::to_string(_status->GetBufferBack()).c_str());
            SetDlgItemText(hwnd, IDC_EDIT_BUFFER_BACK_OVERTIME, std::to_string(_status->GetBufferBackOvertime()).c_str());
            SetDlgItemText(hwnd, IDC_EDIT_SAMPLE_TIME_OFFSET, std::to_string(_status->GetSampleTimeOffset()).c_str());
            break;
        }
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}