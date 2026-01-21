#include "DriverClient.hpp"
#include "..\\Shared\\Public.h"

static bool Ioctl(HANDLE h, DWORD code, const void* inBuf, DWORD inCb)
{
    DWORD ret = 0;
    return !!DeviceIoControl(h, code, (void*)inBuf, inCb, nullptr, 0, &ret, nullptr);
}

bool DeviceIoctlSetRole(HANDLE h, UINT32 role)
{
    KBLAY_SET_ROLE_INPUT in{};
    in.Role = role;
    return Ioctl(h, IOCTL_KBLAY_SET_ROLE, &in, sizeof(in));
}

bool DeviceIoctlSetState(HANDLE h, UINT32 state)
{
    KBLAY_SET_STATE_INPUT in{};
    in.State = state;
    return Ioctl(h, IOCTL_KBLAY_SET_STATE, &in, sizeof(in));
}

bool DeviceIoctlSetRuleBlob(HANDLE h, const std::vector<BYTE>& blob)
{
    if (blob.empty()) return false;
    return Ioctl(h, IOCTL_KBLAY_SET_RULE_BLOB, blob.data(), (DWORD)blob.size());
}
