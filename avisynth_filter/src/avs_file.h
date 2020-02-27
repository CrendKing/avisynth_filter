#pragma once

DECLARE_INTERFACE_(IAvsFile, IUnknown) {
    STDMETHOD(GetAvsFile)(std::string &avsFile) const PURE;
    STDMETHOD(UpdateAvsFile)(const std::string &avsFile) PURE;
    STDMETHOD(ReloadAvsFile)() PURE;
};
