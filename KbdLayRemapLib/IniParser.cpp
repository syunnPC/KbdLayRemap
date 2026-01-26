#include "IniParser.hpp"
#include <fstream>
#include <sstream>

static std::wstring Trim(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool IniParser::Load(const std::wstring& path)
{
    data_.clear();
    std::wifstream ifs(path);
    if (!ifs) return false;
    try
    {
        ifs.imbue(std::locale(""));
    }
    catch (...)
    {
        // Fallback to the default "C" locale if the system locale is unavailable.
    }

    std::wstring section;
    std::wstring line;
    while (std::getline(ifs, line))
    {
        line = Trim(line);
        if (line.empty()) continue;
        if (line[0] == L';' || line[0] == L'#') continue;

        if (line.front() == L'[' && line.back() == L']')
        {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = Trim(line.substr(0, eq));
        std::wstring val = Trim(line.substr(eq + 1));
        data_[section][key] = val;
    }
    return true;
}

std::wstring IniParser::Get(const std::wstring& section, const std::wstring& key, const std::wstring& def) const
{
    auto itS = data_.find(section);
    if (itS == data_.end()) return def;
    auto itK = itS->second.find(key);
    if (itK == itS->second.end()) return def;
    return itK->second;
}
