#include "DeviceId.hpp"
#include "..\\Shared\\Public.h"

#include <SetupAPI.h>
#include <initguid.h>
#include <ntddkbd.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <Rpc.h>
#include <cwchar>
#include <vector>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Cfgmgr32.lib")
#pragma comment(lib, "Rpcrt4.lib")

static std::wstring GetDevPropString(HDEVINFO h, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    wchar_t stackBuf[256] = {};
    DWORD cb = sizeof(stackBuf);

    if (SetupDiGetDevicePropertyW(h, &dev, &key, &type, (PBYTE)stackBuf, cb, &cb, 0))
    {
        if (type == DEVPROP_TYPE_STRING)
            return std::wstring(stackBuf);
        return L"";
    }

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || cb == 0)
        return L"";

    std::vector<BYTE> buf(cb);
    type = 0;
    if (SetupDiGetDevicePropertyW(h, &dev, &key, &type, buf.data(), cb, &cb, 0) && type == DEVPROP_TYPE_STRING)
        return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()));

    return L"";
}

static GUID GetDevPropGuid(HDEVINFO h, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    GUID g = GUID_NULL;
    DWORD cb = sizeof(g);

    if (SetupDiGetDevicePropertyW(h, &dev, &key, &type, (PBYTE)&g, cb, &cb, 0))
    {
        if (type == DEVPROP_TYPE_GUID)
            return g;
        return GUID_NULL;
    }

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || cb == 0)
        return GUID_NULL;

    std::vector<BYTE> buf(cb);
    type = 0;
    if (SetupDiGetDevicePropertyW(h, &dev, &key, &type, buf.data(), cb, &cb, 0) &&
        type == DEVPROP_TYPE_GUID && cb >= sizeof(GUID))
    {
        GUID out = GUID_NULL;
        memcpy(&out, buf.data(), sizeof(out));
        return out;
    }

    return GUID_NULL;
}

static bool IsRootKeyboardInterfacePath(const std::wstring& path)
{
    const wchar_t kPrefix[] = L"\\\\?\\root#keyboard#";
    const size_t len = (sizeof(kPrefix) / sizeof(kPrefix[0])) - 1;
    if (path.size() < len)
        return false;
    return _wcsnicmp(path.c_str(), kPrefix, len) == 0;
}

static std::vector<FilterDeviceInfo> EnumerateByInterfaceGuid(const GUID& guid)
{
    std::vector<FilterDeviceInfo> out;

    HDEVINFO h = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (h == INVALID_HANDLE_VALUE) return out;

    for (DWORD i = 0;; ++i)
    {
        SP_DEVICE_INTERFACE_DATA ifd{};
        ifd.cbSize = sizeof(ifd);
        if (!SetupDiEnumDeviceInterfaces(h, nullptr, &guid, i, &ifd))
            break;

        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(h, &ifd, nullptr, 0, &need, nullptr);
        if (need == 0) continue;

        std::vector<BYTE> buf(need);
        auto* det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA dev{};
        dev.cbSize = sizeof(dev);

        if (!SetupDiGetDeviceInterfaceDetailW(h, &ifd, det, need, nullptr, &dev))
            continue;

        FilterDeviceInfo info{};
        info.DevicePath = det->DevicePath;
        if (IsRootKeyboardInterfacePath(info.DevicePath))
            continue;

        // ContainerId groups devnodes for one physical device.
        info.ContainerId = GetDevPropGuid(h, dev, DEVPKEY_Device_ContainerId);

        info.FriendlyName = GetDevPropString(h, dev, DEVPKEY_Device_FriendlyName);
        if (info.FriendlyName.empty())
            info.FriendlyName = GetDevPropString(h, dev, DEVPKEY_Device_DeviceDesc);

        out.push_back(std::move(info));
    }

    SetupDiDestroyDeviceInfoList(h);
    return out;
}

static bool HasReferenceString(const std::wstring& path, const std::wstring& ref)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return false;
    return _wcsicmp(path.c_str() + pos + 1, ref.c_str()) == 0;
}

static std::vector<FilterDeviceInfo> EnumerateKeyboardInterfacesWithRef(const std::wstring& ref)
{
    auto list = EnumerateByInterfaceGuid(GUID_DEVINTERFACE_KEYBOARD);
    std::vector<FilterDeviceInfo> out;
    out.reserve(list.size());
    for (auto& d : list)
    {
        if (HasReferenceString(d.DevicePath, ref))
            out.push_back(std::move(d));
    }
    return out;
}

std::vector<FilterDeviceInfo> EnumerateKbdLayFilterDevices()
{
    // Prefer the dedicated interface from our filter driver.
    auto out = EnumerateByInterfaceGuid(GUID_DEVINTERFACE_KbdLayRemap);
    if (!out.empty())
        return out;

    // Fallback: enumerate only keyboard interfaces that our filter registered
    // via a reference string.
    out = EnumerateKeyboardInterfacesWithRef(L"KbdLayRemap");
    if (!out.empty())
        return out;

    // Last resort: enumerate standard keyboard interfaces.
    return EnumerateByInterfaceGuid(GUID_DEVINTERFACE_KEYBOARD);
}

std::wstring GuidToString(const GUID& g)
{
    RPC_WSTR s = nullptr;
    if (UuidToStringW((UUID*)&g, &s) != RPC_S_OK || !s)
        return L"";
    std::wstring r = (wchar_t*)s;
    RpcStringFreeW(&s);
    return L"{" + r + L"}";
}

std::vector<GUID> ParseGuidList(const std::wstring& semicolonSeparated)
{
    std::vector<GUID> v;
    size_t start = 0;
    while (start < semicolonSeparated.size())
    {
        size_t end = semicolonSeparated.find(L';', start);
        if (end == std::wstring::npos) end = semicolonSeparated.size();
        std::wstring token = semicolonSeparated.substr(start, end - start);

        // trim
        auto trim = [](std::wstring s) {
            size_t a = s.find_first_not_of(L" \t\r\n");
            if (a == std::wstring::npos) return std::wstring();
            size_t b = s.find_last_not_of(L" \t\r\n");
            return s.substr(a, b - a + 1);
            };
        token = trim(token);

        if (!token.empty())
        {
            GUID g = GUID_NULL;
            RPC_WSTR w = (RPC_WSTR)token.c_str();
            if (UuidFromStringW(w, (UUID*)&g) == RPC_S_OK)
                v.push_back(g);
        }

        start = end + 1;
    }
    return v;
}
