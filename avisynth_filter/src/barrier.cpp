#include "pch.h"
#include "barrier.h"


namespace AvsFilter {

Barrier::Barrier(int count)
    : _initialCount(count)
    , _currentCount(count) {
}

auto Barrier::Arrive() -> void {
    std::unique_lock lock(_mutex);

    _currentCount -= 1;
    if (_currentCount <= 0) {
        _waitCv.notify_all();
    }

    _arriveCv.wait(lock, [this]() -> bool {
        return _currentCount == _initialCount;
    });
}

auto Barrier::Wait() -> void {
    std::shared_lock lock(_mutex);

    _waitCv.wait(lock, [this]() -> bool {
        return _currentCount <= 0;
    });
}

auto Barrier::Unlock() -> void {
    {
        const std::unique_lock lock(_mutex);
        _currentCount = _initialCount;
    }

    _arriveCv.notify_all();
}

}
