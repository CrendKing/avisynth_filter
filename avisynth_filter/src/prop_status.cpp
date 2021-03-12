// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "prop_status.h"
#include "api.h"
#include "constants.h"
#include "format.h"
#include "util.h"


namespace AvsFilter {

CAvsFilterPropStatus::CAvsFilterPropStatus(LPUNKNOWN pUnk, HRESULT *phr)
    : CBasePropertyPage(STATUS_NAME_FULL, pUnk, IDD_STATUS_PAGE, IDS_STATUS)
    , _filter(nullptr)
    , _isSourcePathSet(false) {
}

auto CAvsFilterPropStatus::OnConnect(IUnknown *pUnk) -> HRESULT {
    CheckPointer(pUnk, E_POINTER);
    return pUnk->QueryInterface(IID_IAvsFilter, reinterpret_cast<void **>(&_filter));
}

auto CAvsFilterPropStatus::OnDisconnect() -> HRESULT {
    if (_filter != nullptr) {
        _filter->Release();
        _filter = nullptr;
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

        const std::wstring inputFrameRateStr = DoubleToString(static_cast<double>(_filter->frameHandler.GetCurrentInputFrameRate()) / FRAME_RATE_SCALE_FACTOR, frameRatePrecision);
        const std::wstring outputFrameRateStr = DoubleToString(static_cast<double>(_filter->frameHandler.GetCurrentOutputFrameRate()) / FRAME_RATE_SCALE_FACTOR, frameRatePrecision);
        const std::wstring outputParStr = DoubleToString(static_cast<double>(videoFormat.pixelAspectRatio) / PAR_SCALE_FACTOR, parPrecision);

        SetDlgItemTextW(hwnd, IDC_TEXT_INPUT_BUFFER_SIZE_VALUE, std::to_wstring(_filter->frameHandler.GetInputBufferSize()).c_str());
        SetDlgItemTextW(hwnd, IDC_TEXT_OUTPUT_BUFFER_SIZE_VALUE, std::to_wstring(_filter->frameHandler.GetOutputBufferSize()).c_str());

        SetDlgItemTextW(hwnd, IDC_TEXT_FRAME_NUMBER_VALUE,
                        (std::to_wstring(_filter->frameHandler.GetSourceFrameNb())
                        + L" -> "
                        + std::to_wstring(_filter->frameHandler.GetOutputFrameNb())
                        + L" -> "
                        + std::to_wstring(_filter->frameHandler.GetDeliveryFrameNb()))
                        .c_str());

        SetDlgItemTextW(hwnd, IDC_TEXT_FRAME_RATE_VALUE, (inputFrameRateStr + L" -> " + outputFrameRateStr).c_str());
        SetDlgItemTextW(hwnd, IDC_TEXT_PAR_VALUE, outputParStr.c_str());
        SetDlgItemTextW(hwnd, IDC_TEXT_WORKER_THREAD_COUNT_VALUE, std::to_wstring(_filter->frameHandler.GetOutputWorkerThreadCount()).c_str());

        if (!_isSourcePathSet) {
            std::wstring videoSourcePath = _filter->GetVideoSourcePath();
            if (videoSourcePath.empty()) {
                videoSourcePath = UNAVAILABLE_SOURCE_PATH;
            }
            SetDlgItemTextW(hwnd, IDC_EDIT_PATH_VALUE, videoSourcePath.c_str());
            _isSourcePathSet = true;
        }

        const std::wstring infoStr = std::to_wstring(videoFormat.bmi.biWidth) + L" x "+ std::to_wstring(abs(videoFormat.bmi.biHeight)) + L" " + videoFormat.pixelFormat->name;
        SetDlgItemTextW(hwnd, IDC_TEXT_FORMAT_VALUE, infoStr.c_str());

        return 0;
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

}
