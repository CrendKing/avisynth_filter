// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace SynthFilter {

template <typename T>
class RefCountedSingleton {
public:
    static constexpr auto GetInstance() -> T & {
        return *_instance;
    }

    static constexpr auto Create() -> void {
        _instance = std::make_unique<T>();
    }

    static constexpr auto Destroy() -> void {
        _instance.reset();
    }

private:
    static inline std::unique_ptr<T> _instance;
};

}
