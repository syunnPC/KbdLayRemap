#pragma once

#include <ntddk.h>
#include <wdf.h>

NTSTATUS KbdLayControlDeviceInitialize(_In_ WDFDRIVER Driver);
VOID KbdLayDeviceListAdd(_In_ WDFDEVICE Device);
VOID KbdLayDeviceListRemove(_In_ WDFDEVICE Device);
