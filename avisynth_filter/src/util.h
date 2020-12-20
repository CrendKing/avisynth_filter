// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#pragma once

#include "pch.h"


namespace AvsFilter {

auto ConvertWideToUtf8(const std::wstring &wideString) -> std::string;
auto ConvertUtf8ToWide(const std::string &utf8String) -> std::wstring;
auto DoubleToString(double d, int precision) -> std::string;
auto JoinStrings(const std::vector<std::wstring> &input, wchar_t delimiter) -> std::wstring;

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
