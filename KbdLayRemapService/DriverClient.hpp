#pragma once
#include <Windows.h>
#include <vector>
#include <string>

HANDLE OpenControlDevice(DWORD desiredAccess);

bool DeviceIoctlSetRole(HANDLE h, UINT32 role);
bool DeviceIoctlSetState(HANDLE h, UINT32 state);
bool DeviceIoctlSetRuleBlob(HANDLE h, const std::vector<BYTE>& blob);

bool DeviceIoctlSetRoleEx(HANDLE h, const GUID& containerId, UINT32 role);
bool DeviceIoctlSetStateEx(HANDLE h, const GUID& containerId, UINT32 state);
bool DeviceIoctlSetRuleBlobEx(HANDLE h, const GUID& containerId, const std::vector<BYTE>& blob);
