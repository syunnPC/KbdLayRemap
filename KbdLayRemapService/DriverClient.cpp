#include "DriverClient.hpp"
#include "..\\Shared\\Public.h"
#include <cstddef>

static bool Ioctl(HANDLE h, DWORD code, const void* inBuf, DWORD inCb)
{
    DWORD ret = 0;
    return !!DeviceIoControl(h, code, (void*)inBuf, inCb, nullptr, 0, &ret, nullptr);
}

HANDLE OpenControlDevice(DWORD desiredAccess)
{
    return CreateFileW(
        KBLAY_CONTROL_DEVICE_DOS_NAME,
        desiredAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
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

bool DeviceIoctlSetRoleEx(HANDLE h, const GUID& containerId, UINT32 role)
{
    KBLAY_SET_ROLE_EX_INPUT in{};
    in.ContainerId = containerId;
    in.Role = role;
    return Ioctl(h, IOCTL_KBLAY_SET_ROLE_EX, &in, sizeof(in));
}

bool DeviceIoctlSetStateEx(HANDLE h, const GUID& containerId, UINT32 state)
{
    KBLAY_SET_STATE_EX_INPUT in{};
    in.ContainerId = containerId;
    in.State = state;
    return Ioctl(h, IOCTL_KBLAY_SET_STATE_EX, &in, sizeof(in));
}

bool DeviceIoctlSetRuleBlobEx(HANDLE h, const GUID& containerId, const std::vector<BYTE>& blob)
{
    if (blob.empty()) return false;

    const size_t header = offsetof(KBLAY_SET_RULE_BLOB_EX_INPUT, Blob);
    const size_t total = header + blob.size();

    std::vector<BYTE> buf(total);
    auto* in = reinterpret_cast<KBLAY_SET_RULE_BLOB_EX_INPUT*>(buf.data());
    in->ContainerId = containerId;
    in->BlobSize = static_cast<UINT32>(blob.size());
    memcpy(in->Blob, blob.data(), blob.size());

    return Ioctl(h, IOCTL_KBLAY_SET_RULE_BLOB_EX, buf.data(), static_cast<DWORD>(buf.size()));
}
