#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winioctl.h>
#endif

#include "KbdLayGuids.h"
#include "KbdLayRules.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum KBLAY_ROLE
    {
        KBLAY_ROLE_NONE = 0,
        KBLAY_ROLE_BASE = 1, // "JIS side" (no remap)
        KBLAY_ROLE_REMAP = 2  // "US side" (remap enabled)
    } KBLAY_ROLE;

    typedef enum KBLAY_STATE
    {
        KBLAY_STATE_BYPASS_HARD = 0,
        KBLAY_STATE_BYPASS_SOFT = 1,
        KBLAY_STATE_ACTIVE = 2
    } KBLAY_STATE;

#pragma pack(push, 1)

    typedef struct KBLAY_SET_ROLE_INPUT
    {
        UINT32 Role; // KBLAY_ROLE
    } KBLAY_SET_ROLE_INPUT;

    typedef struct KBLAY_SET_STATE_INPUT
    {
        UINT32 State; // KBLAY_STATE
    } KBLAY_SET_STATE_INPUT;

    typedef struct KBLAY_STATUS_OUTPUT
    {
        UINT32 Role;   // KBLAY_ROLE
        UINT32 State;  // KBLAY_STATE

        UINT64 RemapHitCount;
        UINT64 PassThroughCount;
        UINT64 UnmappedCount;
        UINT64 ShiftToggleCount;

        UINT32 LastErrorNtStatus; // 0 if none
        UINT32 Reserved;

        GUID   ContainerId; // best-effort (GUID_NULL if unknown)
    } KBLAY_STATUS_OUTPUT;

#pragma pack(pop)

    // IOCTL function codes
#define KBLAY_IOCTL_BASE  0x800

#define IOCTL_KBLAY_SET_ROLE      CTL_CODE(FILE_DEVICE_UNKNOWN, KBLAY_IOCTL_BASE + 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KBLAY_SET_STATE     CTL_CODE(FILE_DEVICE_UNKNOWN, KBLAY_IOCTL_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KBLAY_SET_RULE_BLOB CTL_CODE(FILE_DEVICE_UNKNOWN, KBLAY_IOCTL_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KBLAY_GET_STATUS    CTL_CODE(FILE_DEVICE_UNKNOWN, KBLAY_IOCTL_BASE + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

#ifdef __cplusplus
}
#endif
