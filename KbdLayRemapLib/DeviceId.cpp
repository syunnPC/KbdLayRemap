#include "DeviceId.hpp"
#include "..\\Shared\\Public.h"

#include <SetupAPI.h>
#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <Rpc.h>
#include <vector>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Cfgmgr32.lib")
#pragma comment(lib, "Rpcrt4.lib")

static std::wstring GetDevPropString(HDEVINFO h, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    wchar_t buf[512] = {};
    DWORD cb = sizeof(buf);

    if (SetupDiGetDevicePropertyW(h, &dev, &key, &type, (PBYTE)buf, cb, &cb, 0) && type == DEVPROP_TYPE_STRING)
        return std::wstring(buf);

    return L"";
}

static GUID GetDevPropGuid(HDEVINFO h, SP_DEVINFO_DATA& dev, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    GUID g = GUID_NULL;
    DWORD cb = sizeof(g);

    if (SetupDiGetDevicePropertyW(h, &dev, &key, &type, (PBYTE)&g, cb, &cb, 0) && type == DEVPROP_TYPE_GUID)
        return g;

    return GUID_NULL;
}

std::vector<FilterDeviceInfo> EnumerateKbdLayFilterDevices()
{
    std::vector<FilterDeviceInfo> out;

    HDEVINFO h = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_KbdLayRemap, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (h == INVALID_HANDLE_VALUE) return out;

    for (DWORD i = 0;; ++i)
    {
        SP_DEVICE_INTERFACE_DATA ifd{};
        ifd.cbSize = sizeof(ifd);
        if (!SetupDiEnumDeviceInterfaces(h, nullptr, &GUID_DEVINTERFACE_KbdLayRemap, i, &ifd))
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
