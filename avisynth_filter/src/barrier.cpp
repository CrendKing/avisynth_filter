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
    _waitCv.notify_one();
    _arriveCv.wait(lock);
}

auto Barrier::Wait() -> void {
    std::shared_lock lock(_mutex);

    while (_currentCount > 0) {
        _waitCv.wait(lock);
    }
}

auto Barrier::Unlock() -> void {
    const std::unique_lock lock(_mutex);

    _arriveCv.notify_all();
    _currentCount = _initialCount;
}

}
