#pragma once

#include "pch.h"


DECLARE_INTERFACE_(IAvsFilterSettings, IUnknown) {
    virtual auto STDMETHODCALLTYPE SaveSettings() const -> void = 0;

    virtual auto STDMETHODCALLTYPE GetAvsFile() const -> const std::string & = 0;
    virtual auto STDMETHODCALLTYPE SetAvsFile(const std::string & avsFile) -> void = 0;

    virtual auto STDMETHODCALLTYPE GetReloadAvsFile() const -> bool = 0;
    virtual auto STDMETHODCALLTYPE SetReloadAvsFile(bool reload) -> void = 0;

    virtual auto STDMETHODCALLTYPE GetBufferBack() const -> int = 0;
    virtual auto STDMETHODCALLTYPE SetBufferBack(int bufferBack) -> void = 0;
    virtual auto STDMETHODCALLTYPE GetBufferAhead() const -> int = 0;
    virtual auto STDMETHODCALLTYPE SetBufferAhead(int bufferBack) -> void = 0;

    virtual auto STDMETHODCALLTYPE GetInputFormats() const -> DWORD = 0;
    virtual auto STDMETHODCALLTYPE SetInputFormats(DWORD formatBits) -> void = 0;
};