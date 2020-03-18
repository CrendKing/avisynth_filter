#pragma once

#include "pch.h"
#include "registry.h"


DECLARE_INTERFACE_(IAvsFilterSettings, IUnknown) {
    virtual auto LoadSettings() -> void = 0;
    virtual auto SaveSettings() const -> void = 0;

    virtual auto GetAvsFile() const -> const std::string & = 0;
    virtual auto SetAvsFile(const std::string & avsFile) -> void = 0;
    virtual auto ReloadAvsFile() -> void = 0;

    virtual auto GetBufferBack() const -> int = 0;
    virtual auto SetBufferBack(int bufferBack) -> void = 0;
    virtual auto GetBufferAhead() const -> int = 0;
    virtual auto SetBufferAhead(int bufferBack) -> void = 0;

    virtual auto GetFormats() const -> const std::unordered_set<int> & = 0;
    virtual auto SetFormats(const std::unordered_set<int> & formatIndices) -> void = 0;
    virtual auto IsFormatSupported(int formatIndex) const -> bool = 0;
};

class CAviSynthFilter;

class CAvsFilterSettings
    : public IAvsFilterSettings
    , CUnknown {

public:
    CAvsFilterSettings(LPUNKNOWN pUnk, HRESULT *phr, CAviSynthFilter &filter);

    DECLARE_IUNKNOWN

    auto LoadSettings() -> void override;
    auto SaveSettings() const -> void override;

    auto GetAvsFile() const -> const std::string & override;
    auto SetAvsFile(const std::string &avsFile) -> void override;
    auto ReloadAvsFile() -> void override;

    auto GetBufferBack() const -> int override;
    auto SetBufferBack(int bufferBack) -> void override;
    auto GetBufferAhead() const -> int override;
    auto SetBufferAhead(int bufferAhead) -> void override;

    auto GetFormats() const -> const std::unordered_set<int> & override;
    auto SetFormats(const std::unordered_set<int> &formatIndices) -> void override;
    auto IsFormatSupported(int formatIndex) const -> bool override;

private:
    CAviSynthFilter &_filter;
    Registry _registry;

    std::string _avsFile;
    int _bufferBack;
    int _bufferAhead;
    std::unordered_set<int> _formatIndices;
};