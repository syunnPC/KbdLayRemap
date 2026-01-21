#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include "..\\KbdLayRemapLib\\DeviceId.hpp"

static void PrintUsage()
{
    std::wcout << L"Usage:\n"
        << L"  kblayctl list\n";
}

int wmain(int argc, wchar_t** argv)
{
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

    PrintUsage();
    return 1;
}
