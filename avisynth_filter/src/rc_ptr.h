#pragma once

#include "pch.h"


namespace AvsFilter {

template <typename T>
class ReferenceCountPointer {
public:
    explicit ReferenceCountPointer()
        : _ptr(new T())
        , _ref(0) {
    }

    auto operator->() const -> T * {
        return _ptr;
    }

    auto operator!() const -> bool {
        return !_ptr;
    }

    auto AddRef() -> void {
        _ref += 1;
    }

    auto Release() -> void {
        _ref -= 1;

        if (_ref == 0) {
            delete _ptr;
        }
    }

private:
    T *_ptr;
    int _ref;
};

}
