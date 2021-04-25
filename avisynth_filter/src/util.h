// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once


namespace AvsFilter {

auto ConvertWideToUtf8(const std::wstring &wideString) -> std::string;
auto ConvertUtf8ToWide(const std::string &utf8String) -> std::wstring;
auto DoubleToString(double d, int precision) -> std::wstring;
auto JoinStrings(const std::vector<std::wstring> &input, WCHAR delimiter) -> std::wstring;

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
template<std::ranges::input_range R, class T, class ProjFind = std::identity, class ProjOut = std::identity>
requires std::indirect_binary_predicate<std::ranges::equal_to,
                                        std::projected<std::ranges::iterator_t<R>, ProjFind>,
                                        const T *>
auto OptionalFind(R &&r, const T &value, ProjFind projFind = {}, ProjOut projOut = {}) {
    const auto iter = std::ranges::find(r, value, projFind);
    return iter == r.end() ? std::nullopt : std::optional(std::invoke(projOut, *iter));
}

}
