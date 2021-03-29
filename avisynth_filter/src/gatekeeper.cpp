// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "gatekeeper.h"


namespace AvsFilter {

Gatekeeper::Gatekeeper(int count)
    : _initialCount(count)
    , _currentCount(count) {
}

auto Gatekeeper::ArriveAndWait(std::unique_lock<std::mutex> &lock) -> void {
    _currentCount -= 1;
    if (_currentCount <= 0) {
        _waitCv.notify_all();
    }

    _arriveCv.wait(lock, [this]() -> bool {
        return _currentCount == _initialCount;
    });
}

auto Gatekeeper::WaitForCount(std::unique_lock<std::mutex> &lock) -> void {
    _waitCv.wait(lock, [this]() -> bool {
        return _currentCount <= 0;
    });
}

auto Gatekeeper::OpenGate() -> void {
    _currentCount = _initialCount;
    _arriveCv.notify_all();
}

}
