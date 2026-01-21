#include <Windows.h>
#include <iostream>
#include <vector>

#include "ServiceConfig.hpp"
#include "DriverClient.hpp"
#include "..\\Shared\\KbdLayIoctl.h"

#include "..\\KbdLayRemapLib\\DeviceId.hpp"
#include "..\\KbdLayRemapLib\\RuleBlob.hpp"

static bool GuidInList(const GUID& g, const std::vector<GUID>& list)
{
    for (auto& x : list) if (IsEqualGUID(g, x)) return true;
    return false;
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        std::wstring iniPath = (argc >= 2) ? argv[1] : L"KbdLayRemap.ini";
        auto cfg = LoadConfigOrDie(iniPath);

        // base layout is cfg.BaseKlid; target is the other one
        const std::wstring base = cfg.BaseKlid;
        const std::wstring other = (base == L"00000411") ? L"00000409" : L"00000411";

        // build rules for "target layout" on top of base
        // If base is JIS, then US keyboard should target US; if base is US, then JIS keyboard should target JIS.
        // Here we generate rules for (base -> other) to be used by ROLE_REMAP device.
        auto blob = BuildUsJisRuleBlob(base, other);

        auto devs = EnumerateKbdLayFilterDevices();
        if (devs.empty())
        {
            std::wcerr << L"No filter devices found. Is the driver installed and device interface created?\n";
            return 2;
        }

        // Apply: unknown => NONE+BYPASS, JIS => BASE+BYPASS, US => REMAP+ACTIVE(+rules)
        for (auto& d : devs)
        {
            HANDLE h = CreateFileW(
                d.DevicePath.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);

            if (h == INVALID_HANDLE_VALUE)
            {
                std::wcerr << L"Open failed: " << d.DevicePath << L"\n";
                continue;
            }

            const bool isUs = GuidInList(d.ContainerId, cfg.UsContainers);
            const bool isJis = GuidInList(d.ContainerId, cfg.JisContainers);

            UINT32 role = KBLAY_ROLE_NONE;
            UINT32 state = KBLAY_STATE_BYPASS_HARD;

            if (isUs && !isJis)
            {
                role = KBLAY_ROLE_REMAP;
                state = KBLAY_STATE_ACTIVE;
                DeviceIoctlSetRuleBlob(h, blob);
            }
            else if (isJis && !isUs)
            {
                role = KBLAY_ROLE_BASE;
                state = KBLAY_STATE_ACTIVE; // base side still active, but driver does pass-through for BASE role
            }

            DeviceIoctlSetRole(h, role);
            DeviceIoctlSetState(h, state);

            CloseHandle(h);

            std::wcout << L"[APPLY] "
                << (d.FriendlyName.empty() ? L"(unknown)" : d.FriendlyName)
                << L" ContainerId=" << GuidToString(d.ContainerId)
                << L" Role=" << role << L" State=" << state << L"\n";
        }

        std::wcout << L"Applied. (This is minimal service. ExcludeHard/Soft loop will be added next.)\n";
        return 0;
    }
    catch (...)
    {
        std::wcerr << L"Service failed.\n";
        return 1;
    }
}
