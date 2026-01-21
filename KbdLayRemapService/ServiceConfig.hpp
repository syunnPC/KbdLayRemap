#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <guiddef.h>

struct ServiceConfig
{
    std::wstring IniPath;

    std::vector<GUID> UsContainers;
    std::vector<GUID> JisContainers;

    std::wstring BaseKlid;   // "00000411" or "00000409" or "Auto"
};

ServiceConfig LoadConfigOrDie(const std::wstring& iniPath);
