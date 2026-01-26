#include "RuleBlob.hpp"
#include "..\\Shared\\Public.h"
#include <vector>

static HKL LoadKlid(const std::wstring& klid)
{
    // Best-effort load; does not need to become active.
    return LoadKeyboardLayoutW(klid.c_str(), KLF_NOTELLSHELL);
}

static std::wstring GetCharForScan(HKL hkl, UINT sc, bool shift)
{
    if (!hkl) return L"";

    BYTE ks[256]{};
    if (shift)
    {
        ks[VK_SHIFT] = 0x80;
        ks[VK_LSHIFT] = 0x80;
    }

    // Map scancode to VK for this layout
    UINT vk = MapVirtualKeyExW(sc, MAPVK_VSC_TO_VK_EX, hkl);
    if (vk == 0) return L"";

    wchar_t buf[8]{};
    int r = ToUnicodeEx(vk, sc, ks, buf, 8, 0, hkl);
    if (r == 1)
        return std::wstring(buf, 1);
    if (r < 0)
    {
        // Clear dead-key state.
        (void)ToUnicodeEx(vk, sc, ks, buf, 8, 0, hkl);
        return L"";
    }

    // dead keys / multi chars are ignored (best-effort)
    return L"";
}

static int Cost(int sameScan, int sameShift)
{
    // prefer same scan & same shift (0), then same scan diff shift (1), then diff scan same shift (2), diff+diff (3)
    if (sameScan && sameShift) return 0;
    if (sameScan && !sameShift) return 1;
    if (!sameScan && sameShift) return 2;
    return 3;
}

std::vector<BYTE> BuildUsJisRuleBlob(const std::wstring& baseKlid, const std::wstring& targetKlid)
{
    HKL base = LoadKlid(baseKlid);
    HKL target = LoadKlid(targetKlid);
    if (!base || !target)
    {
        if (base) UnloadKeyboardLayout(base);
        if (target) UnloadKeyboardLayout(target);
        return {};
    }

    std::vector<KBLAY_RULE_ENTRY> entries;

    for (int inShift = 0; inShift <= 1; ++inShift)
    {
        for (UINT inSc = 0; inSc <= 0x7F; ++inSc) // practical range
        {
            std::wstring want = GetCharForScan(target, inSc, inShift != 0);
            if (want.empty()) continue;

            int bestCost = 999;
            UINT bestSc = inSc;
            int bestShift = inShift;

            for (int outShift = 0; outShift <= 1; ++outShift)
            {
                for (UINT outSc = 0; outSc <= 0x7F; ++outSc)
                {
                    std::wstring got = GetCharForScan(base, outSc, outShift != 0);
                    if (got == want)
                    {
                        int c = Cost(outSc == inSc, outShift == inShift);
                        if (c < bestCost)
                        {
                            bestCost = c;
                            bestSc = outSc;
                            bestShift = outShift;
                            if (bestCost == 0) goto found_best;
                        }
                    }
                }
            }
        found_best:

            // Only emit if it changes something (scan or desired shift)
            if (bestCost != 999 && (bestSc != inSc || bestShift != inShift))
            {
                KBLAY_RULE_ENTRY e{};
                e.InMakeCode = (UINT8)inSc;
                e.InFlags = (inShift ? KBLAY_FLAG_SHIFT : 0);

                e.OutMakeCode = (UINT8)bestSc;
                e.OutFlags = (bestShift ? KBLAY_FLAG_SHIFT : 0);

                entries.push_back(e);
            }
        }
    }

    KBLAY_RULE_BLOB_HEADER h{};
    h.Version = KBLAY_RULE_BLOB_VERSION;
    h.EntryCount = (UINT32)entries.size();
    h.TotalSizeBytes = (UINT32)(sizeof(KBLAY_RULE_BLOB_HEADER) + entries.size() * sizeof(KBLAY_RULE_ENTRY));
    h.Reserved = 0;

    std::vector<BYTE> blob(h.TotalSizeBytes);
    memcpy(blob.data(), &h, sizeof(h));
    if (!entries.empty())
        memcpy(blob.data() + sizeof(h), entries.data(), entries.size() * sizeof(KBLAY_RULE_ENTRY));

    UnloadKeyboardLayout(base);
    UnloadKeyboardLayout(target);
    return blob;
}
