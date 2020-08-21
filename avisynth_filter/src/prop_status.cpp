#include "pch.h"
#include "prop_status.h"
#include "constants.h"
#include "format.h"


CAvsFilterPropStatus::CAvsFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(NAME(STATUS_FULL), pUnk, IDD_STATUSPAGE, IDS_STATUS)
    , _status(nullptr)
    , _isSourcePathSet(false)
    , _prevInputSampleNb(-1)
    , _prevDeliveryFrameNb(-1) {
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
    if (SetTimer(m_hwnd, IDT_TIMER_STATUS, STATUS_PAGE_TIMER_INTERVAL_MS, nullptr) == 0) {
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
            SetDlgItemTextA(hwnd, IDC_TEXT_BUFFER_SIZE_VALUE, std::to_string(_status->GetBufferSize()).c_str());
            SetDlgItemTextA(hwnd, IDC_TEXT_BUFFER_AHEAD_VALUE, std::to_string(_status->GetBufferUnderflowAhead()).c_str());
            SetDlgItemTextA(hwnd, IDC_TEXT_BUFFER_BACK_VALUE, std::to_string(_status->GetBufferUnderflowBack()).c_str());
            SetDlgItemTextA(hwnd, IDC_TEXT_SAMPLE_TIME_OFFSET_VALUE, std::to_string(_status->GetSampleTimeOffset()).c_str());

            const auto [inputSampleNb, deliverFrameNb] = _status->GetFrameNumbers();
            SetDlgItemTextA(hwnd, IDC_TEXT_FRAME_NUMBER_VALUE, (std::to_string(inputSampleNb).append(" / ").append(std::to_string(deliverFrameNb))).c_str());

            if (_prevInputSampleNb != -1 && _prevDeliveryFrameNb != -1) {
                const int inputSampleNbDiff = inputSampleNb - _prevInputSampleNb;
                const int deliveryFrameNbDiff = deliverFrameNb - _prevDeliveryFrameNb;
                if (inputSampleNbDiff > 0) {
                    const int inputFrameRate = MulDiv(inputSampleNbDiff, 1000, STATUS_PAGE_TIMER_INTERVAL_MS);
                    const int deliveryFrameRate = MulDiv(deliveryFrameNbDiff, 1000, STATUS_PAGE_TIMER_INTERVAL_MS);
                    SetDlgItemTextA(hwnd, IDC_TEXT_FRAME_RATE_VALUE, std::to_string(inputFrameRate).append(" / ").append(std::to_string(deliveryFrameRate)).c_str());
                }
            }
            _prevInputSampleNb = inputSampleNb;
            _prevDeliveryFrameNb = deliverFrameNb;

            if (!_isSourcePathSet) {
                SetDlgItemTextW(hwnd, IDC_EDIT_PATH_VALUE, _status->GetSourcePath().c_str());
                _isSourcePathSet = true;
            }

            const Format::VideoFormat *format = _status->GetMediaInfo();
            const std::string infoStr = std::to_string(format->bmi.biWidth).append(" x ").append(std::to_string(abs(format->bmi.biHeight))).append(" ").append(format->GetCodecName());
            SetDlgItemTextA(hwnd, IDC_TEXT_FORMAT_VALUE, infoStr.c_str());

            break;
        }
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}