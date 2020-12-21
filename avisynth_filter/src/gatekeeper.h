// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"


namespace AvsFilter {

/**
 * slightly different version of std::barrier that separates the roles between participants and a controller
 */
class Gatekeeper {
public:
    explicit Gatekeeper(int count);

    /**
     * called by the particpant threads to decrement the expected count by one and block until the controller unblocks them
     */
    auto ArriveAndWait() -> void;

    /**
     * called by the controller thread to be blocked until the expected count to reaches zero
     */
    auto WaitForCount() -> void;

    /**
     * called by the controller thread to unblock all participant threads
     */
    auto Unlock() -> void;

private:
    int _initialCount;
    int _currentCount;
    std::shared_mutex _mutex;
    std::condition_variable_any _arriveCv;
    std::condition_variable_any _waitCv;
};

}
