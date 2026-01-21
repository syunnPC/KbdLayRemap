#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define KBLAY_RULE_BLOB_VERSION 0x00010000u

    // InFlags / OutFlags bit layout
#define KBLAY_FLAG_E0        0x01u
#define KBLAY_FLAG_SHIFT     0x02u

#pragma pack(push, 1)

    typedef struct KBLAY_RULE_BLOB_HEADER
    {
        UINT32 Version;         // KBLAY_RULE_BLOB_VERSION
        UINT32 EntryCount;      // number of KBLAY_RULE_ENTRY records following
        UINT32 TotalSizeBytes;  // sizeof(header) + EntryCount*sizeof(entry)
        UINT32 Reserved;        // must be 0
    } KBLAY_RULE_BLOB_HEADER;

    typedef struct KBLAY_RULE_ENTRY
    {
        UINT8  InMakeCode;   // 0..255
        UINT8  InFlags;      // KBLAY_FLAG_E0 | KBLAY_FLAG_SHIFT
        UINT8  OutMakeCode;  // 0..255
        UINT8  OutFlags;     // KBLAY_FLAG_E0 | KBLAY_FLAG_SHIFT (SHIFT=desired shift state during MAKE)
    } KBLAY_RULE_ENTRY;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif
