#include <Windows.h>
#include <iostream>
#include <locale>
#include <string>
#include <vector>
#include "..\\KbdLayRemapLib\\DeviceId.hpp"
#include "..\\Shared\\Public.h"

static void PrintUsage()
{
    std::wcout << L"Usage:\n"
        << L"  kblayctl list\n"
        << L"  kblayctl status [index]\n"
        << L"  kblayctl containers\n";
}

static void PrintStatus(HANDLE h, const FilterDeviceInfo& dev)
{
    if (IsEqualGUID(dev.ContainerId, GUID_NULL))
    {
        std::wcout << L"    ContainerId is null; status unavailable.\n";
        return;
    }

    KBLAY_GET_STATUS_EX_INPUT in{};
    in.ContainerId = dev.ContainerId;

    KBLAY_STATUS_OUTPUT out{};
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(
        h,
        IOCTL_KBLAY_GET_STATUS_EX,
        &in,
        sizeof(in),
        &out,
        sizeof(out),
        &ret,
        nullptr);

    if (!ok)
    {
        DWORD e = GetLastError();
        std::wcout << L"    IOCTL_KBLAY_GET_STATUS_EX failed: " << e << L"\n";
        return;
    }

    std::wcout << L"    Role=" << out.Role
        << L" State=" << out.State
        << L" RemapHit=" << out.RemapHitCount
        << L" Pass=" << out.PassThroughCount
        << L" Unmapped=" << out.UnmappedCount
        << L" ShiftToggle=" << out.ShiftToggleCount
        << L" LastNt=0x" << std::hex << out.LastErrorNtStatus << std::dec
        << L"\n";

}

static int PrintDriverContainers()
{
    HANDLE h = CreateFileW(
        KBLAY_CONTROL_DEVICE_DOS_NAME,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        std::wcout << L"Open control device failed: " << e << L"\n";
        return 3;
    }

    std::vector<BYTE> buf(4096);
    DWORD ret = 0;
    BOOL ok = FALSE;
    DWORD err = 0;

    for (int i = 0; i < 3; ++i)
    {
        ret = 0;
        ok = DeviceIoControl(
            h,
            IOCTL_KBLAY_ENUM_DEVICES,
            nullptr,
            0,
            buf.data(),
            (DWORD)buf.size(),
            &ret,
            nullptr);
        if (ok)
            break;
        err = GetLastError();
        if (err != ERROR_MORE_DATA && err != ERROR_INSUFFICIENT_BUFFER)
            break;
        buf.resize(buf.size() * 2);
    }

    if (!ok)
    {
        std::wcout << L"IOCTL_KBLAY_ENUM_DEVICES failed: " << err << L"\n";
        CloseHandle(h);
        return 3;
    }

    auto* out = reinterpret_cast<KBLAY_ENUM_DEVICES_OUTPUT*>(buf.data());
    std::wcout << L"Driver devices: " << out->ReturnedCount
        << L" (total " << out->TotalCount << L")\n";
    for (UINT32 i = 0; i < out->ReturnedCount; ++i)
    {
        const auto& info = out->Devices[i];
        std::wcout << L"  [" << i << L"] ";
        if (info.HasContainerId)
            std::wcout << GuidToString(info.ContainerId) << L"\n";
        else
            std::wcout << L"(null)\n";
    }

    CloseHandle(h);
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        std::wcout.imbue(std::locale(""));
    }
    catch (...)
    {
        // Keep default locale if the system locale is unavailable.
    }

    if (argc < 2) { PrintUsage(); return 1; }

    std::wstring cmd = argv[1];
    if (cmd == L"list")
    {
        auto devs = EnumerateKbdLayFilterDevices();
        for (size_t i = 0; i < devs.size(); ++i)
        {
            std::wcout << L"[" << i << L"] "
                << (devs[i].FriendlyName.empty() ? L"(unknown)" : devs[i].FriendlyName)
                << L"\n    ContainerId=" << GuidToString(devs[i].ContainerId)
                << L"\n    DevicePath=" << devs[i].DevicePath
                << L"\n";
        }
        return 0;
    }
    if (cmd == L"status")
    {
        auto devs = EnumerateKbdLayFilterDevices();
        if (devs.empty())
        {
            std::wcout << L"No devices found.\n";
            return 2;
        }

        HANDLE hCtrl = CreateFileW(
            KBLAY_CONTROL_DEVICE_DOS_NAME,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (hCtrl == INVALID_HANDLE_VALUE)
        {
            DWORD e = GetLastError();
            std::wcout << L"Open control device failed: " << e << L"\n";
            return 3;
        }

        if (argc >= 3)
        {
            size_t idx = _wtoi(argv[2]);
            if (idx >= devs.size())
            {
                std::wcout << L"Index out of range.\n";
                CloseHandle(hCtrl);
                return 2;
            }
            std::wcout << L"[" << idx << L"] "
                << (devs[idx].FriendlyName.empty() ? L"(unknown)" : devs[idx].FriendlyName)
                << L"\n    ContainerId=" << GuidToString(devs[idx].ContainerId)
                << L"\n    DevicePath=" << devs[idx].DevicePath
                << L"\n";
            PrintStatus(hCtrl, devs[idx]);
            CloseHandle(hCtrl);
            return 0;
        }

        for (size_t i = 0; i < devs.size(); ++i)
        {
            std::wcout << L"[" << i << L"] "
                << (devs[i].FriendlyName.empty() ? L"(unknown)" : devs[i].FriendlyName)
                << L"\n    ContainerId=" << GuidToString(devs[i].ContainerId)
                << L"\n    DevicePath=" << devs[i].DevicePath
                << L"\n";
            PrintStatus(hCtrl, devs[i]);
        }
        CloseHandle(hCtrl);
        return 0;
    }
    if (cmd == L"containers")
    {
        return PrintDriverContainers();
    }

    PrintUsage();
    return 1;
}
