#pragma once
#include <Windows.h>
#include <vector>
#include <string>

bool DeviceIoctlSetRole(HANDLE h, UINT32 role);
bool DeviceIoctlSetState(HANDLE h, UINT32 state);
bool DeviceIoctlSetRuleBlob(HANDLE h, const std::vector<BYTE>& blob);
