#include "pch.h"
#include "settings.h"
#include "constants.h"
#include "format.h"


auto CALLBACK CAvsFilterSettings::CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) -> CUnknown * {
    CAvsFilterSettings *newInstance = new CAvsFilterSettings(pUnk, phr);

    if (newInstance == nullptr) {
        *phr = E_OUTOFMEMORY;
    }

    return newInstance;
}

CAvsFilterSettings::CAvsFilterSettings(LPUNKNOWN pUnk, HRESULT *phr)
    : CUnknown(NAME(SETTINGS_NAME), pUnk, phr) {
}

auto STDMETHODCALLTYPE CAvsFilterSettings::NonDelegatingQueryInterface(REFIID riid, void **ppv) -> HRESULT {
    CheckPointer(ppv, E_POINTER);

    if (riid == IID_ISpecifyPropertyPages) {
        return GetInterface(static_cast<ISpecifyPropertyPages *>(this), ppv);
    }

    if (riid == IID_IAvsFilterSettings) {
        return GetInterface(static_cast<IAvsFilterSettings *>(this), ppv);
    }

    return CUnknown::NonDelegatingQueryInterface(riid, ppv);
}

auto STDMETHODCALLTYPE CAvsFilterSettings::GetPages(CAUUID *pPages) -> HRESULT {
    CheckPointer(pPages, E_POINTER);

    pPages->pElems = static_cast<GUID *>(CoTaskMemAlloc(sizeof(GUID)));
    if (pPages->pElems == nullptr) {
        return E_OUTOFMEMORY;
    }

    pPages->cElems = 1;
    pPages->pElems[0] = CLSID_AvsPropertyPage;

    return S_OK;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::LoadSettings() -> void {
    _avsFile = _registry.ReadString(REGISTRY_VALUE_NAME_AVS_FILE);

    const DWORD regBufferBack = _registry.ReadNumber(REGISTRY_VALUE_NAME_BUFFER_BACK);
    if (regBufferBack == REGISTRY_INVALID_NUMBER || regBufferBack < BUFFER_FRAMES_MIN || regBufferBack > BUFFER_FRAMES_MAX) {
        _bufferBack = BUFFER_BACK_DEFAULT;
    } else {
        _bufferBack = regBufferBack;
    }

    const DWORD regBufferAhead = _registry.ReadNumber(REGISTRY_VALUE_NAME_BUFFER_AHEAD);
    if (regBufferAhead == REGISTRY_INVALID_NUMBER || regBufferAhead < BUFFER_FRAMES_MIN || regBufferAhead > BUFFER_FRAMES_MAX) {
        _bufferAhead = BUFFER_AHEAD_DEFAULT;
    } else {
        _bufferAhead = regBufferAhead;
    }

    DWORD regFormatIndices = _registry.ReadNumber(REGISTRY_VALUE_NAME_FORMATS);
    if (regBufferAhead == REGISTRY_INVALID_NUMBER) {
        regFormatIndices = (1 << Format::FORMATS.size()) - 1;
    }

    for (int i = 0; i < Format::FORMATS.size(); ++i) {
        if ((regFormatIndices & (1 << i)) != 0) {
            _formatIndices.insert(i);
        }
    }
}

auto STDMETHODCALLTYPE CAvsFilterSettings::SaveSettings() const -> void {
    DWORD regFormatIndices = 0;
    for (int i : _formatIndices) {
        regFormatIndices |= (1 << i);
    }

    _registry.WriteString(REGISTRY_VALUE_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_BUFFER_BACK, _bufferBack);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_BUFFER_AHEAD, _bufferAhead);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_FORMATS, regFormatIndices);
}

auto STDMETHODCALLTYPE CAvsFilterSettings::GetAvsFile() const -> const std::string & {
    return _avsFile;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::SetAvsFile(const std::string &avsFile) -> void {
    _avsFile = avsFile;
}


auto STDMETHODCALLTYPE CAvsFilterSettings::GetReloadAvsFile() const -> bool {
    return _reloadAvsFile;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::SetReloadAvsFile(bool reload) -> void {
    _reloadAvsFile = reload;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::GetBufferBack() const -> int {
    return _bufferBack;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::SetBufferBack(int bufferBack) -> void {
    _bufferBack = bufferBack;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::GetBufferAhead() const -> int {
    return _bufferAhead;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::SetBufferAhead(int bufferAhead) -> void {
    _bufferAhead = bufferAhead;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::GetFormats() const -> const std::unordered_set<int> & {
    return _formatIndices;
}

auto STDMETHODCALLTYPE CAvsFilterSettings::SetFormats(const std::unordered_set<int> &formatIndices) -> void {
    _formatIndices = formatIndices;
}

auto CAvsFilterSettings::IsFormatSupported(int formatIndex) const -> bool {
    return _formatIndices.find(formatIndex) != _formatIndices.end();
}
