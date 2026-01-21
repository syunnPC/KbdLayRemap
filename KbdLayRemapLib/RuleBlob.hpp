#pragma once
#include <Windows.h>
#include <vector>
#include <string>

std::vector<BYTE> BuildUsJisRuleBlob(const std::wstring& baseKlid, const std::wstring& targetKlid);
