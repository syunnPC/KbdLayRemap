#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <kbdmou.h>   // CONNECT_DATA, IOCTL_INTERNAL_KEYBOARD_CONNECT, PSERVICE_CALLBACK_ROUTINE

#include "..\\Shared\\Public.h"

typedef struct KBLAY_RULE_CELL
{
    UINT8  OutMakeCode;
    UINT8  OutFlags;    // KBLAY_FLAG_E0 | KBLAY_FLAG_SHIFT
    UINT16 Reserved;
} KBLAY_RULE_CELL;

typedef struct KBDLAY_DEVICE_CONTEXT
{
    volatile LONG Role;   // KBLAY_ROLE
    volatile LONG State;  // KBLAY_STATE

    CONNECT_DATA UpperConnect;     // original class connect data
    BOOLEAN      UpperConnectValid;

    // physical modifier state as seen from hardware events
    BOOLEAN PhysShift;
    BOOLEAN PhysCtrl;
    BOOLEAN PhysAlt;
    BOOLEAN PhysWin;

    // rules: [inE0][inShift][inMakeCode]
    KBLAY_RULE_CELL Rules[2][2][256];
    BOOLEAN         RuleValid[2][2][256];

    // stats
    volatile LONG64 RemapHitCount;
    volatile LONG64 PassThroughCount;
    volatile LONG64 UnmappedCount;
    volatile LONG64 ShiftToggleCount;
    volatile LONG   LastErrorNtStatus;

    GUID ContainerId; // best-effort cache (GUID_NULL if unknown)

    WDFSPINLOCK Lock;
} KBDLAY_DEVICE_CONTEXT, * PKBDLAY_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(KBDLAY_DEVICE_CONTEXT, KbdLayGetDeviceContext)

EVT_WDF_DEVICE_CONTEXT_CLEANUP KbdLayEvtDeviceContextCleanup;

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL KbdLayEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL KbdLayEvtIoInternalDeviceControl;
