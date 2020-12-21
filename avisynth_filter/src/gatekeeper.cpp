// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "gatekeeper.h"


namespace AvsFilter {

Gatekeeper::Gatekeeper(int count)
    : _initialCount(count)
    , _currentCount(count) {
}

auto Gatekeeper::ArriveAndWait() -> void {
    std::unique_lock lock(_mutex);

    _currentCount -= 1;
    if (_currentCount <= 0) {
        _waitCv.notify_all();
    }

    _arriveCv.wait(lock, [this]() -> bool {
        return _currentCount == _initialCount;
    });
}

auto Gatekeeper::WaitForCount() -> void {
    std::shared_lock lock(_mutex);

    _waitCv.wait(lock, [this]() -> bool {
        return _currentCount <= 0;
    });
}

auto Gatekeeper::Unlock() -> void {
    {
        const std::unique_lock lock(_mutex);
        _currentCount = _initialCount;
    }

    _arriveCv.notify_all();
}

}
