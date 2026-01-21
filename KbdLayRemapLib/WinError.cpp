#include "WinError.hpp"

std::wstring WinErrorMessage(DWORD code)
{
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&buf, 0, nullptr);

    std::wstring s = (n && buf) ? std::wstring(buf, n) : L"(unknown)";
    if (buf) LocalFree(buf);
    return s;
}
