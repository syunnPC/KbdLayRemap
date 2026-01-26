#include "Device.h"
#include "ControlDevice.h"
#include "KeyboardConnect.h"
#include "IoctlQueue.h"
#include "RemapEngine.h"

#include <initguid.h>
#include <devpropdef.h>
#include <devpkey.h>
#include <wdfdevice.h>

static const GUID KBDLAY_GUID_NULL = { 0 };
// Standard keyboard device interface GUID.
static const GUID KBDLAY_GUID_DEVINTERFACE_KEYBOARD =
{ 0x884b96c3, 0x56ef, 0x11d1, { 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd } };

static VOID KbdLayStoreContainerId(_In_ PKBDLAY_DEVICE_CONTEXT Ctx, _In_ const GUID* ContainerId);

NTSTATUS
KbdLayEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDeviceSelfManagedIoInit = KbdLayEvtDeviceSelfManagedIoInit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

    // Enforce device object access policy.
    UNICODE_STRING sddl;
    RtlInitUnicodeString(&sddl, KBLAY_DEVICE_SDDL);

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
    ctx->Listed = FALSE;
    ctx->Device = device;
    InitializeListHead(&ctx->ListEntry);

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &ctx->Lock);
    if (!NT_SUCCESS(status)) return status;

    KbdLayRemapInit(ctx);
    KbdLayRefreshContainerId(device);
    KbdLayDeviceListAdd(device);

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_KbdLayRemap, NULL);
    if (!NT_SUCCESS(status))
        ctx->LastErrorNtStatus = status;

    // Also register a keyboard-class interface with a reference string so
    // user-mode can reliably open the filter device even if the custom class
    // interface is missing.
    UNICODE_STRING ref;
    RtlInitUnicodeString(&ref, L"KbdLayRemap");
    status = WdfDeviceCreateDeviceInterface(device, &KBDLAY_GUID_DEVINTERFACE_KEYBOARD, &ref);
    if (!NT_SUCCESS(status))
        ctx->LastErrorNtStatus = status;

    // Default queue handles external IOCTLs.
    WDF_IO_QUEUE_CONFIG qcfg;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
    qcfg.EvtIoDeviceControl = KbdLayEvtIoDeviceControl;

    WDFQUEUE queue = NULL;
    status = WdfIoQueueCreate(device, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) return status;

    // Internal IOCTLs are serialized to avoid connect/disconnect overlap.
    WDF_IO_QUEUE_CONFIG iqcfg;
    WDF_IO_QUEUE_CONFIG_INIT(&iqcfg, WdfIoQueueDispatchSequential);
    iqcfg.EvtIoInternalDeviceControl = KbdLayEvtIoInternalDeviceControl;

    WDFQUEUE iqueue = NULL;
    status = WdfIoQueueCreate(device, &iqcfg, WDF_NO_OBJECT_ATTRIBUTES, &iqueue);
    if (!NT_SUCCESS(status)) return status;

    status = WdfDeviceConfigureRequestDispatching(
        device,
        iqueue,
        WdfRequestTypeDeviceControlInternal);
    return status;
}

VOID
KbdLayEvtDeviceContextCleanup(_In_ WDFOBJECT DeviceObject)
{
    KbdLayDeviceListRemove((WDFDEVICE)DeviceObject);
}

NTSTATUS
KbdLayEvtDeviceSelfManagedIoInit(_In_ WDFDEVICE Device)
{
    KbdLayRefreshContainerId(Device);
    return STATUS_SUCCESS;
}

static VOID
KbdLayStoreContainerId(_In_ PKBDLAY_DEVICE_CONTEXT Ctx, _In_ const GUID* ContainerId)
{
    if (!ContainerId || IsEqualGUID(ContainerId, &KBDLAY_GUID_NULL))
        return;

    WdfSpinLockAcquire(Ctx->Lock);
    Ctx->ContainerId = *ContainerId;
    WdfSpinLockRelease(Ctx->Lock);
}

VOID
KbdLayRefreshContainerId(_In_ WDFDEVICE Device)
{
    // Best-effort: query unified device property model.
    // ContainerId groups devnodes belonging to one physical device.
    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(Device);

    // Prefer querying the PDO directly (filter FDOs may not expose properties).
    PDEVICE_OBJECT pdo = WdfDeviceWdmGetPhysicalDevice(Device);
    if (pdo)
    {
        GUID g = KBDLAY_GUID_NULL;
        DEVPROPTYPE type = 0;
        ULONG req = 0;
        NTSTATUS s = IoGetDevicePropertyData(
            pdo,
            &DEVPKEY_Device_ContainerId,
            LOCALE_NEUTRAL,
            0,
            sizeof(g),
            &g,
            &req,
            &type);

        if (NT_SUCCESS(s) && type == DEVPROP_TYPE_GUID)
        {
            KbdLayStoreContainerId(ctx, &g);
            return;
        }
    }

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

    if (!NT_SUCCESS(status))
    {
        if (mem) WdfObjectDelete(mem);
        return;
    }
    if (propType != DEVPROP_TYPE_GUID || mem == NULL)
    {
        if (mem) WdfObjectDelete(mem);
        return;
    }

    size_t cb = 0;
    GUID* g = (GUID*)WdfMemoryGetBuffer(mem, &cb);
    if (g && cb >= sizeof(GUID))
        KbdLayStoreContainerId(ctx, g);

    WdfObjectDelete(mem);
}
