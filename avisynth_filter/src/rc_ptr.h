// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"


namespace AvsFilter {

template <typename T>
class ReferenceCountPointer {
public:
    constexpr ReferenceCountPointer()
        : _ptr(nullptr)
        , _ref(0) {
    }

    constexpr auto operator=(T *ptr) -> ReferenceCountPointer<T> & {
        if (_ptr != nullptr) {
            throw std::invalid_argument("already has stored pointer");
        }

        _ptr = ptr;
        _ref = 1;
        return *this;
    }

    constexpr auto operator->() const -> T * {
        return _ptr;
    }

    constexpr explicit operator bool() const {
        return _ptr != nullptr;
    }

    constexpr auto AddRef() -> void {
        InterlockedIncrement(&_ref);
    }

    constexpr auto Release() -> void {
        if (InterlockedDecrement(&_ref) == 0) {
            std::default_delete<T>()(_ptr);
            _ptr = nullptr;
        }
    }

private:
    T *_ptr;
    long _ref;
};

}
