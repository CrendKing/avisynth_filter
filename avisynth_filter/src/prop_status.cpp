#include "pch.h"
#include "prop_status.h"
#include "api.h"
#include "constants.h"
#include "format.h"
#include "util.h"


namespace AvsFilter {

CAvsFilterPropStatus::CAvsFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(NAME(STATUS_FULL), pUnk, IDD_STATUSPAGE, IDS_STATUS)
    , _status(nullptr)
    , _isSourcePathSet(false) {
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
    if (uMsg == WM_COMMAND) {
        if (HIWORD(wParam) == EN_SETFOCUS) {
            HideCaret(reinterpret_cast<HWND>(lParam));
        }
    } else if (uMsg == WM_TIMER) {
        SetDlgItemTextA(hwnd, IDC_TEXT_BUFFER_SIZE_VALUE, std::to_string(_status->GetBufferSize()).c_str());
        SetDlgItemTextA(hwnd, IDC_TEXT_CURRENT_PREFETCH_VALUE, std::to_string(_status->GetCurrentPrefetch()).c_str());
        SetDlgItemTextA(hwnd, IDC_TEXT_INITIAL_PREFETCH_VALUE, std::to_string(_status->GetInitialPrefetch()).c_str());

        const int sourceSampleNb = _status->GetSourceSampleNumber();
        const int deliverFrameNb = _status->GetDeliveryFrameNumber();
        SetDlgItemTextA(hwnd, IDC_TEXT_FRAME_NUMBER_VALUE, std::to_string(sourceSampleNb).append(" -> ").append(std::to_string(deliverFrameNb)).c_str());

        const int frameRatePrecision = static_cast<int>(log10(FRAME_RATE_SCALE_FACTOR));
        std::string inputFrameRateStr = DoubleToString(static_cast<double>(_status->GetInputFrameRate()) / FRAME_RATE_SCALE_FACTOR, frameRatePrecision);
        const std::string outputFrameRateStr = DoubleToString(static_cast<double>(_status->GetOutputFrameRate()) / FRAME_RATE_SCALE_FACTOR, frameRatePrecision);

        SetDlgItemTextA(hwnd, IDC_TEXT_FRAME_RATE_VALUE, inputFrameRateStr.append(" -> ").append(outputFrameRateStr).c_str());

        if (!_isSourcePathSet) {
            std::wstring videoSourcePath = _status->GetVideoSourcePath();
            if (videoSourcePath.empty()) {
                videoSourcePath = UNAVAILABLE_SOURCE_PATH;
            }
            SetDlgItemTextW(hwnd, IDC_EDIT_PATH_VALUE, videoSourcePath.c_str());
            _isSourcePathSet = true;
        }

        const Format::VideoFormat format = _status->GetInputMediaInfo();
        const std::string infoStr = std::to_string(format.bmi.biWidth).append(" x ").append(std::to_string(abs(format.bmi.biHeight))).append(" ").append(format.GetCodecName());
        SetDlgItemTextA(hwnd, IDC_TEXT_FORMAT_VALUE, infoStr.c_str());
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

}