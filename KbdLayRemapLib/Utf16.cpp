#include "Utf16.hpp"
#include <algorithm>

std::wstring ToLowerAscii(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        if (c >= L'A' && c <= L'Z') return (wchar_t)(c - L'A' + L'a');
        return c;
        });
    return s;
}

std::wstring FileNameOnly(std::wstring fullPath)
{
    size_t p = fullPath.find_last_of(L"\\/");
    if (p == std::wstring::npos) return fullPath;
    return fullPath.substr(p + 1);
}
