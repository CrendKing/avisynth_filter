// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "prop_status.h"

#include "constants.h"


namespace SynthFilter {

CSynthFilterPropStatus::CSynthFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(STATUS_NAME_FULL, pUnk, IDD_STATUS_PAGE, IDS_STATUS) {}

auto CSynthFilterPropStatus::OnConnect(IUnknown *pUnk) -> HRESULT {
    CheckPointer(pUnk, E_POINTER);
    _filter = reinterpret_cast<CSynthFilter *>(pUnk);
    _filter->AddRef();

    return S_OK;
}

auto CSynthFilterPropStatus::OnDisconnect() -> HRESULT {
    if (_filter != nullptr) {
        _filter->Release();
        _filter = nullptr;
    }

    return S_OK;
}

auto CSynthFilterPropStatus::OnActivate() -> HRESULT {
    if (SetTimer(m_hwnd, IDT_TIMER_STATUS, STATUS_PAGE_TIMER_INTERVAL_MS, nullptr) == 0) {
        return E_FAIL;
    }

    return S_OK;
}

auto CSynthFilterPropStatus::OnApplyChanges() -> HRESULT {
    if (!KillTimer(m_hwnd, IDT_TIMER_STATUS)) {
        return E_FAIL;
    }

    return S_OK;
}

auto CSynthFilterPropStatus::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
    switch (uMsg) {
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_SETFOCUS) {
            HideCaret(reinterpret_cast<HWND>(lParam));
            return 0;
        }
        break;

    case WM_TIMER:
        const Format::VideoFormat videoFormat = _filter->GetInputFormat();

        const int frameRatePrecision = static_cast<int>(log10(FRAME_RATE_SCALE_FACTOR));
        const int parPrecision = static_cast<int>(log10(PAR_SCALE_FACTOR));

        const std::wstring inputFrameRateStr = DoubleToString(static_cast<double>(_filter->frameHandler->GetCurrentInputFrameRate()) / FRAME_RATE_SCALE_FACTOR, frameRatePrecision);
        const std::wstring outputFrameRateStr = DoubleToString(static_cast<double>(_filter->frameHandler->GetCurrentOutputFrameRate()) / FRAME_RATE_SCALE_FACTOR, frameRatePrecision);
        const std::wstring deliveryFrameRateStr = DoubleToString(static_cast<double>(_filter->frameHandler->GetCurrentDeliveryFrameRate()) / FRAME_RATE_SCALE_FACTOR, frameRatePrecision);
        const std::wstring outputParStr = DoubleToString(static_cast<double>(videoFormat.pixelAspectRatioNum) / videoFormat.pixelAspectRatioDen, parPrecision);

        SetDlgItemTextW(hwnd, IDC_TEXT_INPUT_BUFFER_SIZE_VALUE, std::to_wstring(_filter->frameHandler->GetInputBufferSize()).c_str());
        SetDlgItemTextW(hwnd, IDC_TEXT_FRAME_NUMBER_VALUE, std::format(L"{} -> {} -> {}",
                        _filter->frameHandler->GetSourceFrameNb(),
                        _filter->frameHandler->GetOutputFrameNb(),
                        _filter->frameHandler->GetDeliveryFrameNb())
                        .c_str());

        SetDlgItemTextW(hwnd, IDC_TEXT_FRAME_RATE_VALUE, std::format(L"{} -> {} -> {}", inputFrameRateStr, outputFrameRateStr, deliveryFrameRateStr).c_str());
        SetDlgItemTextW(hwnd, IDC_TEXT_PAR_VALUE, outputParStr.c_str());

        if (!_isSourcePathSet) {
            std::wstring_view videoSourcePath = _filter->GetVideoSourcePath().c_str();
            if (videoSourcePath.empty()) {
                videoSourcePath = UNAVAILABLE_SOURCE_PATH;
            }
            SetDlgItemTextW(hwnd, IDC_EDIT_PATH_VALUE, videoSourcePath.data());
            _isSourcePathSet = true;
        }

        const std::wstring infoStr = std::format(L"{} x {} {}", videoFormat.bmi.biWidth, abs(videoFormat.bmi.biHeight), videoFormat.pixelFormat->name);
        SetDlgItemTextW(hwnd, IDC_TEXT_FORMAT_VALUE, infoStr.c_str());

        return 0;
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

}
