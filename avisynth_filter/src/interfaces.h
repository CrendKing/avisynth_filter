#pragma once

#include "pch.h"
#include "api.h"
#include "format.h"


namespace AvsFilter {

DECLARE_INTERFACE_(IAvsFilterSettings, IUnknown) {
    virtual auto STDMETHODCALLTYPE GetEffectiveAvsFile() const -> std::wstring = 0;
    virtual auto STDMETHODCALLTYPE SetEffectiveAvsFile(const std::wstring &avsFile) -> void = 0;
    virtual auto STDMETHODCALLTYPE ReloadAvsSource() -> void = 0;

    virtual auto STDMETHODCALLTYPE GetAvsVersionString() -> const char * = 0;
};

DECLARE_INTERFACE_(IAvsFilterStatus, IUnknown) {
    virtual auto STDMETHODCALLTYPE GetInputBufferSize() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetOutputBufferSize() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetSourceSampleNumber() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetOutputSampleNumber() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetDeliveryFrameNumber() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetCurrentInputFrameRate() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetCurrentOutputFrameRate() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetOutputWorkerThreadCount() const -> int = 0;

    virtual auto STDMETHODCALLTYPE GetVideoSourcePath() const -> std::wstring = 0;
    virtual auto STDMETHODCALLTYPE GetInputMediaInfo() const -> Format::VideoFormat = 0;

    virtual auto STDMETHODCALLTYPE GetVideoFilterNames() const -> std::vector<std::wstring> = 0;
    virtual auto STDMETHODCALLTYPE GetSourceAvgFrameRate() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetAvsState() const -> AvsState = 0;
    virtual auto STDMETHODCALLTYPE GetAvsError() const -> std::optional<std::string> = 0;
};

}
