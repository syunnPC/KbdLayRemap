#pragma once
#include <Windows.h>
#include <string>
#include <vector>

struct FilterDeviceInfo
{
    std::wstring DevicePath;   // CreateFile target
    GUID ContainerId;          // DEVPKEY_Device_ContainerId
    std::wstring FriendlyName; // best-effort
};

std::vector<FilterDeviceInfo> EnumerateKbdLayFilterDevices();
std::vector<GUID> ParseGuidList(const std::wstring& semicolonSeparated);
std::wstring GuidToString(const GUID& g);
