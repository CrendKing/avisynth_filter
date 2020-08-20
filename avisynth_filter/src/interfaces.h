#pragma once

#include "pch.h"
#include "format.h"


DECLARE_INTERFACE_(IAvsFilterSettings, IUnknown) {
    virtual auto STDMETHODCALLTYPE SaveSettings() const -> void = 0;

    virtual auto STDMETHODCALLTYPE GetAvsFile() const -> const std::wstring & = 0;
    virtual auto STDMETHODCALLTYPE SetAvsFile(const std::wstring & avsFile) -> void = 0;

    virtual auto STDMETHODCALLTYPE ReloadAvsFile() -> void = 0;

    virtual auto STDMETHODCALLTYPE GetInputFormats() const -> DWORD = 0;
    virtual auto STDMETHODCALLTYPE SetInputFormats(DWORD formatBits) -> void = 0;
};

DECLARE_INTERFACE_(IAvsFilterStatus, IUnknown) {
    virtual auto STDMETHODCALLTYPE GetBufferSize() -> int = 0;

    virtual auto STDMETHODCALLTYPE GetBufferAhead() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetBufferAheadOvertime() -> int = 0;

    virtual auto STDMETHODCALLTYPE GetBufferBack() const -> int = 0;
    virtual auto STDMETHODCALLTYPE GetBufferBackOvertime() -> int = 0;

    virtual auto STDMETHODCALLTYPE GetSampleTimeOffset() const -> int = 0;

    virtual auto STDMETHODCALLTYPE GetFrameNumbers() const -> std::pair<int, int> = 0;
    virtual auto STDMETHODCALLTYPE GetSourceFrameRate() const -> double = 0;
    virtual auto STDMETHODCALLTYPE GetSourcePath() const -> std::wstring = 0;
    virtual auto STDMETHODCALLTYPE GetMediaInfo() const -> const Format::VideoFormat * = 0;
};