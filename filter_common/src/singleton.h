// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace SynthFilter {

/*
 * This class has explicit Create() and Destroy() functions to allow better control on when things happen.
 * For example, the creation of the instance could be lengthy, which is OK in a loading screen but not during video play.
 *
 * This class is thread-safe, if:
 *   1. all Create() happen before all GetInstance();
 *   2. all Destroy() happen after all GetInstance();
 */
template <typename T>
class OnDemandSingleton {
public:
    static constexpr auto GetInstance() -> T & { return *_instance; }

    static constexpr auto Create() -> T & {
        if (_instance == nullptr) {
            std::lock_guard lock(_mutex);

            if (_instance == nullptr) {
                _instance = new T;
            }
        }

        return *_instance;
    }

    static constexpr auto Destroy() -> void {
        delete _instance;
        _instance = nullptr;
    }

private:
    static inline std::atomic<T *> _instance = nullptr;
    static inline std::mutex _mutex;
};

}
