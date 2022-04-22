// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace SynthFilter {

template <typename T>
class OnDemandSingleton {
public:
    static constexpr auto GetInstance() -> T & { return *_instance; }

    static constexpr auto Create() -> T * {
        T *expected = nullptr;
        if (!_instance.compare_exchange_strong(expected, new T)) {
            throw L"Attempt to create duplicate instances for singleton";
        }
        return _instance;
    }

    static constexpr auto Destroy() -> void {
        delete _instance;
        _instance = nullptr;
    }

private:
    static inline std::atomic<T *> _instance = nullptr;
};

}
