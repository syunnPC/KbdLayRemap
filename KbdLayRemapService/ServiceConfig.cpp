#include "ServiceConfig.hpp"
#include "..\\KbdLayRemapLib\\IniParser.hpp"
#include "..\\KbdLayRemapLib\\DeviceId.hpp"

static std::wstring DetectCurrentKlid()
{
    wchar_t klid[KL_NAMELENGTH]{};
    if (GetKeyboardLayoutNameW(klid))
        return std::wstring(klid);
    return L"00000411";
}

ServiceConfig LoadConfigOrDie(const std::wstring& iniPath)
{
    IniParser ini;
    if (!ini.Load(iniPath))
        throw std::runtime_error("INI load failed");

    ServiceConfig c{};
    c.IniPath = iniPath;

    c.UsContainers = ParseGuidList(ini.Get(L"Mapping", L"US", L""));
    c.JisContainers = ParseGuidList(ini.Get(L"Mapping", L"JIS", L""));

    c.BaseKlid = ini.Get(L"Options", L"BaseKlid", L"Auto");
    if (c.BaseKlid == L"Auto" || c.BaseKlid.empty())
        c.BaseKlid = DetectCurrentKlid();

    return c;
}
