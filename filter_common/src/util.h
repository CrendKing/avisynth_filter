// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

namespace SynthFilter {

auto ConvertWideToUtf8(std::wstring_view wideString) -> std::string;
auto ConvertUtf8ToWide(std::string_view utf8String) -> std::wstring;
auto DoubleToString(double num, int precision) -> std::wstring;
auto JoinStrings(const std::vector<std::wstring> &inputs, std::wstring_view delimiter) -> std::wstring;

template <typename T>
constexpr auto CoprimeIntegers(T &a, T &b) -> void {
    if (const T gcd = std::gcd(a, b); gcd > 1) {
        a /= gcd;
        b /= gcd;
    }
}

/**
 * ceil(dividend / divisor), assuming both oprands are positive
 */
constexpr auto DivideRoundUp(int dividend, int divisor) -> int {
    return (dividend + divisor - 1) / divisor;
}

/**
 * find first matched value in range and apply output projection
 * return: std::optional of the return value of the output projection, or std::nullopt if not found
 */
template <std::ranges::input_range R, class T, class ProjFind = std::identity, class ProjOut = std::identity>
    requires std::indirect_binary_predicate<std::ranges::equal_to, std::projected<std::ranges::iterator_t<R>, ProjFind>, const T *>
auto OptionalFind(R &&r, const T &value, ProjFind projFind = {}, ProjOut projOut = {}) {
    const auto iter = std::ranges::find(r, value, projFind);
    return iter == r.end() ? std::nullopt : std::optional(std::invoke(projOut, *iter));
}

}
