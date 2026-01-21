#include "Device.h"
#include "KeyboardConnect.h"
#include "IoctlQueue.h"
#include "RemapEngine.h"

#include <initguid.h>
#include <devpropdef.h>
#include <devpkey.h>
#include <wdfdevice.h>

static const WCHAR KBLAY_SDDL[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;BU)";

static const GUID KBDLAY_GUID_NULL = { 0 };

static VOID KbdLayTryCacheContainerId(_In_ WDFDEVICE Device);

NTSTATUS
KbdLayEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);

    // Enforce device object access policy.
    UNICODE_STRING sddl;
    RtlInitUnicodeString(&sddl, KBLAY_SDDL);

    NTSTATUS sddlStatus = WdfDeviceInitAssignSDDLString(DeviceInit, &sddl);
    if (!NT_SUCCESS(sddlStatus)) return sddlStatus;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, KBDLAY_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = KbdLayEvtDeviceContextCleanup;

    WDFDEVICE device = NULL;
    NTSTATUS status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) return status;

    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(device);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->Role = KBLAY_ROLE_NONE;
    ctx->State = KBLAY_STATE_BYPASS_HARD;
    ctx->ContainerId = KBDLAY_GUID_NULL;

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &ctx->Lock);
    if (!NT_SUCCESS(status)) return status;

    KbdLayRemapInit(ctx);
    KbdLayTryCacheContainerId(device);

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_KbdLayRemap, NULL);
    if (!NT_SUCCESS(status)) return status;

    // Default queue handles both external and internal IOCTLs
    WDF_IO_QUEUE_CONFIG qcfg;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
    qcfg.EvtIoDeviceControl = KbdLayEvtIoDeviceControl;
    qcfg.EvtIoInternalDeviceControl = KbdLayEvtIoInternalDeviceControl;

    WDFQUEUE queue = NULL;
    status = WdfIoQueueCreate(device, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    return status;
}

VOID
KbdLayEvtDeviceContextCleanup(_In_ WDFOBJECT DeviceObject)
{
    UNREFERENCED_PARAMETER(DeviceObject);
}

static VOID
KbdLayTryCacheContainerId(_In_ WDFDEVICE Device)
{
    // Best-effort: query unified device property model.
    // ContainerId groups devnodes belonging to one physical device.
    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(Device);

    WDF_DEVICE_PROPERTY_DATA prop;
    WDF_DEVICE_PROPERTY_DATA_INIT(&prop, &DEVPKEY_Device_ContainerId);
    prop.Lcid = LOCALE_NEUTRAL;
    prop.Flags = 0;

    WDFMEMORY mem = NULL;
    DEVPROPTYPE propType = 0;

    NTSTATUS status = WdfDeviceAllocAndQueryPropertyEx(
        Device,
        &prop,
        NonPagedPoolNx,
        WDF_NO_OBJECT_ATTRIBUTES,
        &mem,
        &propType);

    if (!NT_SUCCESS(status) || propType != DEVPROP_TYPE_GUID || mem == NULL)
        return;

    size_t cb = 0;
    GUID* g = (GUID*)WdfMemoryGetBuffer(mem, &cb);
    if (g && cb >= sizeof(GUID))
    {
        WdfSpinLockAcquire(ctx->Lock);
        ctx->ContainerId = *g;
        WdfSpinLockRelease(ctx->Lock);
    }

    WdfObjectDelete(mem);
}
