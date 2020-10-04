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
    std::shared_mutex _mutex;
    std::condition_variable_any _arriveCv;
    std::condition_variable_any _waitCv;
};

}
