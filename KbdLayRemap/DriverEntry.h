#pragma once
#include <ntddk.h>
#include <wdf.h>

#include "..\\Shared\\Public.h"

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD KbdLayEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP KbdLayEvtDriverContextCleanup;
