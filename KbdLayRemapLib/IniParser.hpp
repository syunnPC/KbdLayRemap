#pragma once
#include <string>
#include <unordered_map>

class IniParser
{
public:
    bool Load(const std::wstring& path);
    std::wstring Get(const std::wstring& section, const std::wstring& key, const std::wstring& def = L"") const;

private:
    using Key = std::wstring;
    using Map = std::unordered_map<Key, std::wstring>;
    std::unordered_map<Key, Map> data_;
};
