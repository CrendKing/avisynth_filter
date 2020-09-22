#include "pch.h"
#include "barrier.h"


namespace AvsFilter {

Barrier::Barrier(int count)
    : _initialCount(count)
    , _currentCount(count) {
}

auto Barrier::Arrive() -> void {
    std::unique_lock<std::mutex> lock(_arriveMutex);

    _currentCount -= 1;
    _waitCv.notify_one();
    _arriveCv.wait(lock);
}

auto Barrier::Wait() -> void {
    std::mutex waiterMutex;
    std::unique_lock<std::mutex> lock(waiterMutex);

    while (_currentCount > 0) {
        _waitCv.wait(lock);
    }
}

auto Barrier::Unlock() -> void {
    _arriveCv.notify_all();
    _currentCount = _initialCount;
}

}
