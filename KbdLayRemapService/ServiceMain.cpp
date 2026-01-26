#include <Windows.h>
#include <string>
#include <vector>
#include <iostream>

#include "ServiceConfig.hpp"
#include "DriverClient.hpp"
#include "..\\Shared\\KbdLayIoctl.h"

#include "..\\KbdLayRemapLib\\DeviceId.hpp"
#include "..\\KbdLayRemapLib\\RuleBlob.hpp"
#include "..\\KbdLayRemapLib\\WinError.hpp"

static constexpr wchar_t kServiceName[] = L"KbdLayRemapService";

static SERVICE_STATUS_HANDLE g_svcHandle = nullptr;
static SERVICE_STATUS g_status{};
static HANDLE g_stopEvent = nullptr;
static HANDLE g_workerThread = nullptr;
static std::wstring g_iniPath;

static void LogDbg(const std::wstring& s)
{
    OutputDebugStringW((s + L"\n").c_str());
}

static void SetSvcState(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHintMs = 0)
{
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = win32ExitCode;
    g_status.dwWaitHint = waitHintMs;

    if (state == SERVICE_START_PENDING)
    {
        g_status.dwControlsAccepted = 0;
        g_status.dwCheckPoint++;
    }
    else if (state == SERVICE_RUNNING)
    {
        g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
        g_status.dwCheckPoint = 0;
        g_status.dwWaitHint = 0;
    }
    else if (state == SERVICE_STOP_PENDING)
    {
        g_status.dwControlsAccepted = 0;
        g_status.dwCheckPoint++;
    }
    else if (state == SERVICE_STOPPED)
    {
        g_status.dwControlsAccepted = 0;
        g_status.dwCheckPoint = 0;
        g_status.dwWaitHint = 0;
    }

    if (g_svcHandle)
        SetServiceStatus(g_svcHandle, &g_status);
}

static bool GuidInList(const GUID& g, const std::vector<GUID>& list)
{
    for (const auto& x : list) if (IsEqualGUID(g, x)) return true;
    return false;
}

static bool IsNullGuid(const GUID& g)
{
    return !!IsEqualGUID(g, GUID_NULL);
}

static bool ApplyOnce()
{
    auto cfg = LoadConfigOrDie(g_iniPath);

    const std::wstring base = cfg.BaseKlid;
    const std::wstring other = (base == L"00000411") ? L"00000409" : L"00000411";

    // rules for (base -> other) on ROLE_REMAP device
    static std::wstring s_lastBase;
    static std::wstring s_lastOther;
    static std::vector<BYTE> s_cachedBlob;

    if (s_cachedBlob.empty() || s_lastBase != base || s_lastOther != other)
    {
        auto newBlob = BuildUsJisRuleBlob(base, other);
        s_cachedBlob.clear();
        s_lastBase = base;
        s_lastOther = other;
        if (!newBlob.empty())
            s_cachedBlob = std::move(newBlob);
    }

    const auto& blob = s_cachedBlob;

    HANDLE hCtrl = OpenControlDevice(GENERIC_READ | GENERIC_WRITE);
    if (hCtrl == INVALID_HANDLE_VALUE)
    {
        LogDbg(L"[SVC] OpenControlDevice failed: " + WinErrorMessage(GetLastError()));
        return false;
    }

    auto devs = EnumerateKbdLayFilterDevices();
    if (devs.empty())
    {
        LogDbg(L"[SVC] No filter devices found (driver not installed / interface missing).");
        CloseHandle(hCtrl);
        return false;
    }

    for (auto& d : devs)
    {
        if (IsNullGuid(d.ContainerId))
        {
            LogDbg(L"[SVC] Skip device with null ContainerId: " + d.DevicePath);
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
            if (!DeviceIoctlSetRuleBlobEx(hCtrl, d.ContainerId, blob))
            {
                LogDbg(L"[SVC] IOCTL_KBLAY_SET_RULE_BLOB failed: " + WinErrorMessage(GetLastError()));
                role = KBLAY_ROLE_NONE;
                state = KBLAY_STATE_BYPASS_HARD;
            }
        }
        else if (isJis && !isUs)
        {
            role = KBLAY_ROLE_BASE;
            state = KBLAY_STATE_ACTIVE; // BASE role is pass-through by driver logic
        }

        bool okRole = DeviceIoctlSetRoleEx(hCtrl, d.ContainerId, role);
        if (!okRole) LogDbg(L"[SVC] IOCTL_KBLAY_SET_ROLE failed: " + WinErrorMessage(GetLastError()));

        bool okState = DeviceIoctlSetStateEx(hCtrl, d.ContainerId, state);
        if (!okState) LogDbg(L"[SVC] IOCTL_KBLAY_SET_STATE failed: " + WinErrorMessage(GetLastError()));

        if (!okRole || !okState)
        {
            // Best-effort fallback to safe state
            DeviceIoctlSetStateEx(hCtrl, d.ContainerId, KBLAY_STATE_BYPASS_HARD);
            DeviceIoctlSetRoleEx(hCtrl, d.ContainerId, KBLAY_ROLE_NONE);
        }
    }

    CloseHandle(hCtrl);
    return true;
}

static DWORD WINAPI WorkerThread(LPVOID)
{
    LogDbg(L"[SVC] Worker started. INI=" + g_iniPath);

    // Initial apply
    try { (void)ApplyOnce(); }
    catch (...) { LogDbg(L"[SVC] ApplyOnce threw an exception."); }

    // Minimal keep-alive loop: re-apply periodically to catch hotplug/re-enumeration.
    while (WaitForSingleObject(g_stopEvent, 5000) == WAIT_TIMEOUT)
    {
        try { (void)ApplyOnce(); }
        catch (...) { LogDbg(L"[SVC] ApplyOnce threw an exception (loop)."); }
    }

    LogDbg(L"[SVC] Worker exiting.");
    return 0;
}

static DWORD WINAPI ServiceCtrlHandlerEx(DWORD ctrl, DWORD, LPVOID, LPVOID)
{
    if (ctrl == SERVICE_CONTROL_STOP)
    {
        SetSvcState(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        if (g_stopEvent) SetEvent(g_stopEvent);
        return NO_ERROR;
    }
    return NO_ERROR;
}

static void WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/)
{
    g_svcHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceCtrlHandlerEx, nullptr);
    if (!g_svcHandle)
        return;

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    g_status.dwControlsAccepted = 0;
    g_status.dwWin32ExitCode = NO_ERROR;
    g_status.dwServiceSpecificExitCode = 0;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;

    SetSvcState(SERVICE_START_PENDING, NO_ERROR, 5000);

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent)
    {
        SetSvcState(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    g_workerThread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    if (!g_workerThread)
    {
        DWORD e = GetLastError();
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        SetSvcState(SERVICE_STOPPED, e, 0);
        return;
    }

    // Important: report RUNNING quickly (avoid 1053).
    SetSvcState(SERVICE_RUNNING);

    // Wait stop
    WaitForSingleObject(g_stopEvent, INFINITE);

    // Join worker
    if (g_workerThread)
    {
        WaitForSingleObject(g_workerThread, INFINITE);
        CloseHandle(g_workerThread);
        g_workerThread = nullptr;
    }

    if (g_stopEvent)
    {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }

    SetSvcState(SERVICE_STOPPED);
}

static std::wstring DefaultIniPathNextToExe()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    auto pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos + 1);
    p += L"KbdLayRemap.ini";
    return p;
}

int wmain(int argc, wchar_t** argv)
{
    // INI path: arg1 or "next to exe"
    g_iniPath = (argc >= 2) ? argv[1] : DefaultIniPathNextToExe();

    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), ServiceMain },
        { nullptr, nullptr }
    };

    if (StartServiceCtrlDispatcherW(table))
        return 0;

    // If not started by SCM, StartServiceCtrlDispatcher fails with ERROR_FAILED_SERVICE_CONTROLLER_CONNECT (1063).
    // In that case, run once for debugging.
    DWORD e = GetLastError();
    LogDbg(L"[SVC] StartServiceCtrlDispatcher failed: " + WinErrorMessage(e));
    try
    {
        bool ok = ApplyOnce();
        return ok ? 0 : 2;
    }
    catch (...)
    {
        return 1;
    }
}
