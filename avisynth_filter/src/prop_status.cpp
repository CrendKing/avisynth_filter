#include "pch.h"
#include "prop_status.h"
#include "constants.h"
#include "logging.h"

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
            SetDlgItemText(hwnd, IDC_TEXT_BUFFER_SIZE_VALUE, std::to_wstring(_status->GetBufferSize()).c_str());
            SetDlgItemText(hwnd, IDC_TEXT_BUFFER_AHEAD_VALUE, (std::to_wstring(_status->GetBufferAhead()) + L" / " + std::to_wstring(_status->GetBufferAheadOvertime())).c_str());
            SetDlgItemText(hwnd, IDC_TEXT_BUFFER_BACK_VALUE, (std::to_wstring(_status->GetBufferBack()) + L" / " + std::to_wstring(_status->GetBufferBackOvertime())).c_str());
            SetDlgItemText(hwnd, IDC_TEXT_SAMPLE_TIME_OFFSET_VALUE, std::to_wstring(_status->GetSampleTimeOffset()).c_str());

            int in, out;
            _status->GetFrameNumbers(in, out);
            SetDlgItemText(hwnd, IDC_TEXT_FRAME_NUMBER_VALUE, (std::to_wstring(in) + L" / " + std::to_wstring(out)).c_str());
            SetDlgItemText(hwnd, IDC_TEXT_FRAME_RATE_VALUE, std::to_wstring(_status->GetFrameRate()).c_str());
            SetDlgItemText(hwnd, IDC_TEXT_PATH_VALUE, _status->GetMediaPath().c_str());

            int w, h;
            DWORD fcc;
            _status->GetMediaInfo(w, h, fcc);
            char fourcc[5];
            if (fcc >> 24 != 0xe4)
                *(reinterpret_cast<DWORD*>(fourcc)) = fcc;
            else memcpy(fourcc,"RGB ",4);
            fourcc[4] = 0;
            std::string fmt = std::to_string(w) + " x " + std::to_string(h) + " " + std::string(fourcc);
            SetDlgItemTextA(hwnd, IDC_TEXT_FORMAT_VALUE, fmt.c_str());

            break;
        }
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}