#include "pch.h"
#include "settings.h"
#include "filter.h"
#include "constants.h"
#include "format.h"


CAvsFilterSettings::CAvsFilterSettings(LPUNKNOWN pUnk, HRESULT *phr, CAviSynthFilter &filter)
    : CUnknown(SETTINGS_NAME, pUnk, phr)
    , _filter(filter) {
}

auto CAvsFilterSettings::LoadSettings() -> void {
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

auto CAvsFilterSettings::SaveSettings() const -> void {
    DWORD regFormatIndices = 0;
    for (int f : _formatIndices) {
        regFormatIndices |= (1 << f);
    }

    _registry.WriteString(REGISTRY_VALUE_NAME_AVS_FILE, _avsFile);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_BUFFER_BACK, _bufferBack);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_BUFFER_AHEAD, _bufferAhead);
    _registry.WriteNumber(REGISTRY_VALUE_NAME_FORMATS, regFormatIndices);
}

auto CAvsFilterSettings::GetAvsFile() const -> const std::string & {
    return _avsFile;
}

auto CAvsFilterSettings::SetAvsFile(const std::string &avsFile) -> void {
    _avsFile = avsFile;
}

auto CAvsFilterSettings::ReloadAvsFile() -> void {
    _filter._reloadAvsFile = true;
}

auto CAvsFilterSettings::GetBufferBack() const -> int {
    return _bufferBack;
}

auto CAvsFilterSettings::SetBufferBack(int bufferBack) -> void {
    _bufferBack = bufferBack;
}

auto CAvsFilterSettings::GetBufferAhead() const -> int {
    return _bufferAhead;
}

auto CAvsFilterSettings::SetBufferAhead(int bufferAhead) -> void {
    _bufferAhead = bufferAhead;
}

auto CAvsFilterSettings::GetFormats() const -> const std::unordered_set<int> & {
    return _formatIndices;
}

auto CAvsFilterSettings::SetFormats(const std::unordered_set<int> &formatIndices) -> void {
    _formatIndices = formatIndices;
}

auto CAvsFilterSettings::IsFormatSupported(int formatIndex) const -> bool {
    return _formatIndices.find(formatIndex) != _formatIndices.end();
}
