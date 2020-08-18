#pragma once

#include "pch.h"


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

    virtual auto STDMETHODCALLTYPE GetFrameNumbers(int& in, int& out) const -> void = 0;
    virtual auto STDMETHODCALLTYPE GetFrameRate() const -> double = 0;
    virtual auto STDMETHODCALLTYPE GetMediaPath() const -> std::wstring = 0;
    virtual auto STDMETHODCALLTYPE GetMediaInfo(int& width, int& heigth, DWORD & fourcc) const -> void = 0;
};