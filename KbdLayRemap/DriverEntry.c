#include "DriverEntry.h"
#include "ControlDevice.h"
#include "Device.h"

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    WDF_DRIVER_CONFIG_INIT(&config, KbdLayEvtDeviceAdd);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = KbdLayEvtDriverContextCleanup;

    WDFDRIVER driver = NULL;
    NTSTATUS status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, &driver);
    if (!NT_SUCCESS(status))
        return status;

    return KbdLayControlDeviceInitialize(driver);
}

VOID
KbdLayEvtDriverContextCleanup(_In_ WDFOBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}
