#pragma once

#include "pch.h"
#include "registry.h"


DECLARE_INTERFACE_(IAvsFilterSettings, IUnknown) {
    virtual auto STDMETHODCALLTYPE LoadSettings() -> void = 0;
    virtual auto STDMETHODCALLTYPE SaveSettings() const -> void = 0;

    virtual auto STDMETHODCALLTYPE GetAvsFile() const -> const std::string & = 0;
    virtual auto STDMETHODCALLTYPE SetAvsFile(const std::string & avsFile) -> void = 0;

    virtual auto STDMETHODCALLTYPE GetReloadAvsFile() const -> bool = 0;
    virtual auto STDMETHODCALLTYPE SetReloadAvsFile(bool reload) -> void = 0;

    virtual auto STDMETHODCALLTYPE GetBufferBack() const -> int = 0;
    virtual auto STDMETHODCALLTYPE SetBufferBack(int bufferBack) -> void = 0;
    virtual auto STDMETHODCALLTYPE GetBufferAhead() const -> int = 0;
    virtual auto STDMETHODCALLTYPE SetBufferAhead(int bufferBack) -> void = 0;

    virtual auto STDMETHODCALLTYPE GetFormats() const -> const std::unordered_set<int> & = 0;
    virtual auto STDMETHODCALLTYPE SetFormats(const std::unordered_set<int> & formatIndices) -> void = 0;
};

class CAvsFilterSettings
    : public IAvsFilterSettings
    , public ISpecifyPropertyPages
    , public CUnknown {
public:
    static auto CALLBACK CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown *;

    CAvsFilterSettings(LPUNKNOWN pUnk, HRESULT *phr);

    DECLARE_IUNKNOWN

    auto STDMETHODCALLTYPE NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT override;
    auto STDMETHODCALLTYPE GetPages(CAUUID *pPages)->HRESULT override;

    auto STDMETHODCALLTYPE LoadSettings() -> void override;
    auto STDMETHODCALLTYPE SaveSettings() const -> void override;

    auto STDMETHODCALLTYPE GetAvsFile() const -> const std::string & override;
    auto STDMETHODCALLTYPE SetAvsFile(const std::string &avsFile) -> void override;

    auto STDMETHODCALLTYPE GetReloadAvsFile() const -> bool override;
    auto STDMETHODCALLTYPE SetReloadAvsFile(bool reload) -> void override;

    auto STDMETHODCALLTYPE GetBufferBack() const -> int override;
    auto STDMETHODCALLTYPE SetBufferBack(int bufferBack) -> void override;
    auto STDMETHODCALLTYPE GetBufferAhead() const -> int override;
    auto STDMETHODCALLTYPE SetBufferAhead(int bufferAhead) -> void override;

    auto STDMETHODCALLTYPE GetFormats() const -> const std::unordered_set<int> & override;
    auto STDMETHODCALLTYPE SetFormats(const std::unordered_set<int> &formatIndices) -> void override;

    auto IsFormatSupported(int formatIndex) const -> bool;

private:
    Registry _registry;

    std::string _avsFile;
    int _bufferBack;
    int _bufferAhead;
    std::unordered_set<int> _formatIndices;

    bool _reloadAvsFile;
};