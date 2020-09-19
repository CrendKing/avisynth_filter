#pragma once

#include "pch.h"


namespace AvsFilter {

class Barrier {
public:
    explicit Barrier(int count);

    auto Arrive() -> void;
    auto Wait() -> void;
    auto Unlock() -> void;

private:
    int _initialCount;
    int _currentCount;
    std::mutex _arriveMutex;
    std::condition_variable _arriveCv;
    std::condition_variable _waitCv;
};

}
