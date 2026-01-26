#include "ControlDevice.h"
#include "Device.h"
#include "RemapEngine.h"

static WDFSPINLOCK g_DeviceListLock = NULL;
static LIST_ENTRY g_DeviceList;
static WDFDEVICE g_ControlDevice = NULL;
static const GUID KBDLAY_GUID_NULL = { 0 };

static __forceinline BOOLEAN KbdLayIsValidRole(_In_ UINT32 Role)
{
    return (Role == (UINT32)KBLAY_ROLE_NONE || Role == (UINT32)KBLAY_ROLE_BASE || Role == (UINT32)KBLAY_ROLE_REMAP) ? TRUE : FALSE;
}

static __forceinline BOOLEAN KbdLayIsValidState(_In_ UINT32 State)
{
    return (State == (UINT32)KBLAY_STATE_BYPASS_HARD || State == (UINT32)KBLAY_STATE_BYPASS_SOFT || State == (UINT32)KBLAY_STATE_ACTIVE) ? TRUE : FALSE;
}

static VOID KbdLaySnapshotStatus(_In_ PKBDLAY_DEVICE_CONTEXT Ctx, _Inout_ KBLAY_STATUS_OUTPUT* Out, _Inout_ BOOLEAN* Have)
{
    if (!*Have)
    {
        Out->Role = (UINT32)InterlockedCompareExchange((volatile LONG*)&Ctx->Role, 0, 0);
        Out->State = (UINT32)InterlockedCompareExchange((volatile LONG*)&Ctx->State, 0, 0);
        Out->LastErrorNtStatus = (UINT32)InterlockedCompareExchange((volatile LONG*)&Ctx->LastErrorNtStatus, 0, 0);
        Out->ContainerId = Ctx->ContainerId;
        *Have = TRUE;
    }

    Out->RemapHitCount += (UINT64)InterlockedCompareExchange64((volatile LONG64*)&Ctx->RemapHitCount, 0, 0);
    Out->PassThroughCount += (UINT64)InterlockedCompareExchange64((volatile LONG64*)&Ctx->PassThroughCount, 0, 0);
    Out->UnmappedCount += (UINT64)InterlockedCompareExchange64((volatile LONG64*)&Ctx->UnmappedCount, 0, 0);
    Out->ShiftToggleCount += (UINT64)InterlockedCompareExchange64((volatile LONG64*)&Ctx->ShiftToggleCount, 0, 0);
}

static VOID KbdLayRefreshAllContainerIds(VOID)
{
    WDFDEVICE devices[32] = { 0 };
    size_t count = 0;

    if (!g_DeviceListLock)
        return;

    WdfSpinLockAcquire(g_DeviceListLock);
    for (PLIST_ENTRY e = g_DeviceList.Flink; e != &g_DeviceList && count < RTL_NUMBER_OF(devices); e = e->Flink)
    {
        PKBDLAY_DEVICE_CONTEXT ctx = CONTAINING_RECORD(e, KBDLAY_DEVICE_CONTEXT, ListEntry);
        devices[count++] = ctx->Device;
    }
    WdfSpinLockRelease(g_DeviceListLock);

    for (size_t i = 0; i < count; ++i)
        KbdLayRefreshContainerId(devices[i]);
}

static NTSTATUS KbdLayApplyRoleByContainerOnce(_In_ const GUID* ContainerId, _In_ UINT32 Role, _Out_ BOOLEAN* Found)
{
    if (!g_DeviceListLock)
        return STATUS_DEVICE_NOT_READY;

    BOOLEAN found = FALSE;
    WdfSpinLockAcquire(g_DeviceListLock);
    for (PLIST_ENTRY e = g_DeviceList.Flink; e != &g_DeviceList; e = e->Flink)
    {
        PKBDLAY_DEVICE_CONTEXT ctx = CONTAINING_RECORD(e, KBDLAY_DEVICE_CONTEXT, ListEntry);
        if (IsEqualGUID(&ctx->ContainerId, ContainerId))
        {
            InterlockedExchange(&ctx->Role, (LONG)Role);
            InterlockedExchange(&ctx->LastErrorNtStatus, STATUS_SUCCESS);
            found = TRUE;
        }
    }
    WdfSpinLockRelease(g_DeviceListLock);

    *Found = found ? TRUE : FALSE;
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS KbdLayApplyRoleByContainer(_In_ const GUID* ContainerId, _In_ UINT32 Role)
{
    BOOLEAN found = FALSE;
    NTSTATUS status = KbdLayApplyRoleByContainerOnce(ContainerId, Role, &found);
    if (found || !NT_SUCCESS(status))
        return status;

    KbdLayRefreshAllContainerIds();
    return KbdLayApplyRoleByContainerOnce(ContainerId, Role, &found);
}

static NTSTATUS KbdLayApplyStateByContainerOnce(_In_ const GUID* ContainerId, _In_ UINT32 State, _Out_ BOOLEAN* Found)
{
    if (!g_DeviceListLock)
        return STATUS_DEVICE_NOT_READY;

    BOOLEAN found = FALSE;
    WdfSpinLockAcquire(g_DeviceListLock);
    for (PLIST_ENTRY e = g_DeviceList.Flink; e != &g_DeviceList; e = e->Flink)
    {
        PKBDLAY_DEVICE_CONTEXT ctx = CONTAINING_RECORD(e, KBDLAY_DEVICE_CONTEXT, ListEntry);
        if (IsEqualGUID(&ctx->ContainerId, ContainerId))
        {
            InterlockedExchange(&ctx->State, (LONG)State);
            InterlockedExchange(&ctx->LastErrorNtStatus, STATUS_SUCCESS);
            found = TRUE;
        }
    }
    WdfSpinLockRelease(g_DeviceListLock);

    *Found = found ? TRUE : FALSE;
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS KbdLayApplyStateByContainer(_In_ const GUID* ContainerId, _In_ UINT32 State)
{
    BOOLEAN found = FALSE;
    NTSTATUS status = KbdLayApplyStateByContainerOnce(ContainerId, State, &found);
    if (found || !NT_SUCCESS(status))
        return status;

    KbdLayRefreshAllContainerIds();
    return KbdLayApplyStateByContainerOnce(ContainerId, State, &found);
}

static NTSTATUS KbdLayApplyRuleBlobByContainerOnce(_In_ const GUID* ContainerId, _In_reads_bytes_(BlobSize) const VOID* Blob, _In_ size_t BlobSize, _Out_ BOOLEAN* Found)
{
    if (!g_DeviceListLock)
        return STATUS_DEVICE_NOT_READY;

    BOOLEAN found = FALSE;
    BOOLEAN anyFail = FALSE;
    NTSTATUS last = STATUS_SUCCESS;

    WdfSpinLockAcquire(g_DeviceListLock);
    for (PLIST_ENTRY e = g_DeviceList.Flink; e != &g_DeviceList; e = e->Flink)
    {
        PKBDLAY_DEVICE_CONTEXT ctx = CONTAINING_RECORD(e, KBDLAY_DEVICE_CONTEXT, ListEntry);
        if (IsEqualGUID(&ctx->ContainerId, ContainerId))
        {
            found = TRUE;
            NTSTATUS s = KbdLayRemapLoadRuleBlob(ctx, Blob, BlobSize);
            if (!NT_SUCCESS(s))
            {
                anyFail = TRUE;
                last = s;
                InterlockedExchange(&ctx->LastErrorNtStatus, (LONG)s);
            }
            else
            {
                InterlockedExchange(&ctx->LastErrorNtStatus, STATUS_SUCCESS);
            }
        }
    }
    WdfSpinLockRelease(g_DeviceListLock);

    *Found = found ? TRUE : FALSE;

    if (!found)
        return STATUS_NOT_FOUND;
    return anyFail ? last : STATUS_SUCCESS;
}

static NTSTATUS KbdLayApplyRuleBlobByContainer(_In_ const GUID* ContainerId, _In_reads_bytes_(BlobSize) const VOID* Blob, _In_ size_t BlobSize)
{
    BOOLEAN found = FALSE;
    NTSTATUS status = KbdLayApplyRuleBlobByContainerOnce(ContainerId, Blob, BlobSize, &found);
    if (found || !NT_SUCCESS(status))
        return status;

    KbdLayRefreshAllContainerIds();
    return KbdLayApplyRuleBlobByContainerOnce(ContainerId, Blob, BlobSize, &found);
}

static NTSTATUS KbdLayGetStatusByContainerOnce(_In_ const GUID* ContainerId, _Out_ KBLAY_STATUS_OUTPUT* Out, _Out_ BOOLEAN* Found)
{
    if (!g_DeviceListLock)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(Out, sizeof(*Out));
    BOOLEAN found = FALSE;

    WdfSpinLockAcquire(g_DeviceListLock);
    for (PLIST_ENTRY e = g_DeviceList.Flink; e != &g_DeviceList; e = e->Flink)
    {
        PKBDLAY_DEVICE_CONTEXT ctx = CONTAINING_RECORD(e, KBDLAY_DEVICE_CONTEXT, ListEntry);
        if (IsEqualGUID(&ctx->ContainerId, ContainerId))
        {
            KbdLaySnapshotStatus(ctx, Out, &found);
        }
    }
    WdfSpinLockRelease(g_DeviceListLock);

    *Found = found ? TRUE : FALSE;
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS KbdLayGetStatusByContainer(_In_ const GUID* ContainerId, _Out_ KBLAY_STATUS_OUTPUT* Out)
{
    BOOLEAN found = FALSE;
    NTSTATUS status = KbdLayGetStatusByContainerOnce(ContainerId, Out, &found);
    if (found || !NT_SUCCESS(status))
        return status;

    KbdLayRefreshAllContainerIds();
    return KbdLayGetStatusByContainerOnce(ContainerId, Out, &found);
}

static NTSTATUS KbdLayEnumContainers(_Out_writes_bytes_(OutBytes) KBLAY_ENUM_CONTAINERS_OUTPUT* Out, _In_ size_t OutBytes)
{
    if (!g_DeviceListLock)
        return STATUS_DEVICE_NOT_READY;

    const size_t header = FIELD_OFFSET(KBLAY_ENUM_CONTAINERS_OUTPUT, Containers);
    if (OutBytes < header)
        return STATUS_BUFFER_TOO_SMALL;

    const size_t cap = (OutBytes - header) / sizeof(GUID);
    Out->Count = 0;

    if (cap == 0)
        return STATUS_BUFFER_TOO_SMALL;

    WdfSpinLockAcquire(g_DeviceListLock);
    for (PLIST_ENTRY e = g_DeviceList.Flink; e != &g_DeviceList; e = e->Flink)
    {
        PKBDLAY_DEVICE_CONTEXT ctx = CONTAINING_RECORD(e, KBDLAY_DEVICE_CONTEXT, ListEntry);
        GUID g = ctx->ContainerId;
        if (IsEqualGUID(&g, &KBDLAY_GUID_NULL))
            continue;

        BOOLEAN seen = FALSE;
        for (UINT32 i = 0; i < Out->Count; ++i)
        {
            if (IsEqualGUID(&Out->Containers[i], &g))
            {
                seen = TRUE;
                break;
            }
        }
        if (seen)
            continue;

        if (Out->Count >= (UINT32)cap)
        {
            WdfSpinLockRelease(g_DeviceListLock);
            return STATUS_BUFFER_OVERFLOW;
        }

        Out->Containers[Out->Count++] = g;
    }
    WdfSpinLockRelease(g_DeviceListLock);
    return STATUS_SUCCESS;
}

static NTSTATUS KbdLayEnumDevices(_Out_writes_bytes_(OutBytes) KBLAY_ENUM_DEVICES_OUTPUT* Out, _In_ size_t OutBytes)
{
    if (!g_DeviceListLock)
        return STATUS_DEVICE_NOT_READY;

    const size_t header = FIELD_OFFSET(KBLAY_ENUM_DEVICES_OUTPUT, Devices);
    if (OutBytes < header)
        return STATUS_BUFFER_TOO_SMALL;

    const size_t cap = (OutBytes - header) / sizeof(KBLAY_ENUM_DEVICE_INFO);
    Out->ReturnedCount = 0;
    Out->TotalCount = 0;

    if (cap == 0)
        return STATUS_BUFFER_TOO_SMALL;

    WdfSpinLockAcquire(g_DeviceListLock);
    for (PLIST_ENTRY e = g_DeviceList.Flink; e != &g_DeviceList; e = e->Flink)
    {
        PKBDLAY_DEVICE_CONTEXT ctx = CONTAINING_RECORD(e, KBDLAY_DEVICE_CONTEXT, ListEntry);
        Out->TotalCount++;

        if (Out->ReturnedCount >= (UINT32)cap)
        {
            WdfSpinLockRelease(g_DeviceListLock);
            return STATUS_BUFFER_OVERFLOW;
        }

        GUID g = ctx->ContainerId;
        Out->Devices[Out->ReturnedCount].ContainerId = g;
        Out->Devices[Out->ReturnedCount].HasContainerId = IsEqualGUID(&g, &KBDLAY_GUID_NULL) ? 0u : 1u;
        Out->ReturnedCount++;
    }
    WdfSpinLockRelease(g_DeviceListLock);
    return STATUS_SUCCESS;
}

static VOID
KbdLayEvtIoControlDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    if (IoControlCode == IOCTL_KBLAY_SET_ROLE_EX)
    {
        KBLAY_SET_ROLE_EX_INPUT* in = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_SET_ROLE_EX_INPUT), (PVOID*)&in, &cb);
        if (NT_SUCCESS(status))
        {
            if (!KbdLayIsValidRole(in->Role))
            {
                status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                status = KbdLayApplyRoleByContainer(&in->ContainerId, in->Role);
            }
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_SET_STATE_EX)
    {
        KBLAY_SET_STATE_EX_INPUT* in = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_SET_STATE_EX_INPUT), (PVOID*)&in, &cb);
        if (NT_SUCCESS(status))
        {
            if (!KbdLayIsValidState(in->State))
            {
                status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                status = KbdLayApplyStateByContainer(&in->ContainerId, in->State);
            }
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_SET_RULE_BLOB_EX)
    {
        KBLAY_SET_RULE_BLOB_EX_INPUT* in = NULL;
        size_t cb = 0;
        const size_t min = FIELD_OFFSET(KBLAY_SET_RULE_BLOB_EX_INPUT, Blob);

        status = WdfRequestRetrieveInputBuffer(Request, min, (PVOID*)&in, &cb);
        if (NT_SUCCESS(status))
        {
            if (cb < min || in->BlobSize == 0 || in->BlobSize > (UINT32)KBLAY_MAX_RULE_BLOB_BYTES)
            {
                status = STATUS_INVALID_BUFFER_SIZE;
            }
            else if (in->BlobSize > cb - min)
            {
                status = STATUS_INVALID_BUFFER_SIZE;
            }
            else
            {
                status = KbdLayApplyRuleBlobByContainer(&in->ContainerId, in->Blob, (size_t)in->BlobSize);
            }
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_GET_STATUS_EX)
    {
        KBLAY_GET_STATUS_EX_INPUT* in = NULL;
        size_t cbIn = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_GET_STATUS_EX_INPUT), (PVOID*)&in, &cbIn);
        if (NT_SUCCESS(status))
        {
            KBLAY_STATUS_OUTPUT* out = NULL;
            size_t cbOut = 0;
            status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KBLAY_STATUS_OUTPUT), (PVOID*)&out, &cbOut);
            if (NT_SUCCESS(status))
            {
                status = KbdLayGetStatusByContainer(&in->ContainerId, out);
                if (NT_SUCCESS(status))
                    WdfRequestSetInformation(Request, sizeof(*out));
            }
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_ENUM_CONTAINERS)
    {
        KBLAY_ENUM_CONTAINERS_OUTPUT* out = NULL;
        size_t cbOut = 0;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KBLAY_ENUM_CONTAINERS_OUTPUT), (PVOID*)&out, &cbOut);
        if (NT_SUCCESS(status))
        {
            status = KbdLayEnumContainers(out, cbOut);
            if (NT_SUCCESS(status))
            {
                const size_t header = FIELD_OFFSET(KBLAY_ENUM_CONTAINERS_OUTPUT, Containers);
                size_t used = header + ((size_t)out->Count * sizeof(GUID));
                WdfRequestSetInformation(Request, used);
            }
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_ENUM_DEVICES)
    {
        KBLAY_ENUM_DEVICES_OUTPUT* out = NULL;
        size_t cbOut = 0;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KBLAY_ENUM_DEVICES_OUTPUT), (PVOID*)&out, &cbOut);
        if (NT_SUCCESS(status))
        {
            status = KbdLayEnumDevices(out, cbOut);
            if (NT_SUCCESS(status))
            {
                const size_t header = FIELD_OFFSET(KBLAY_ENUM_DEVICES_OUTPUT, Devices);
                size_t used = header + ((size_t)out->ReturnedCount * sizeof(KBLAY_ENUM_DEVICE_INFO));
                WdfRequestSetInformation(Request, used);
            }
        }
    }

    WdfRequestComplete(Request, status);
}

NTSTATUS KbdLayControlDeviceInitialize(_In_ WDFDRIVER Driver)
{
    if (g_ControlDevice)
        return STATUS_SUCCESS;

    WDF_OBJECT_ATTRIBUTES lockAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttr);
    lockAttr.ParentObject = Driver;
    NTSTATUS status = WdfSpinLockCreate(&lockAttr, &g_DeviceListLock);
    if (!NT_SUCCESS(status))
        return status;

    InitializeListHead(&g_DeviceList);

    UNICODE_STRING sddl;
    RtlInitUnicodeString(&sddl, KBLAY_DEVICE_SDDL);

    PWDFDEVICE_INIT init = WdfControlDeviceInitAllocate(Driver, &sddl);
    if (!init)
        return STATUS_INSUFFICIENT_RESOURCES;

    UNICODE_STRING name;
    RtlInitUnicodeString(&name, KBLAY_CONTROL_DEVICE_NT_NAME);
    status = WdfDeviceInitAssignName(init, &name);
    if (!NT_SUCCESS(status))
    {
        WdfDeviceInitFree(init);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES devAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&devAttr);
    status = WdfDeviceCreate(&init, &devAttr, &g_ControlDevice);
    if (!NT_SUCCESS(status))
    {
        WdfDeviceInitFree(init);
        return status;
    }

    UNICODE_STRING sym;
    RtlInitUnicodeString(&sym, KBLAY_CONTROL_DEVICE_SYMBOLIC_LINK);
    status = WdfDeviceCreateSymbolicLink(g_ControlDevice, &sym);
    if (!NT_SUCCESS(status))
    {
        WdfObjectDelete(g_ControlDevice);
        g_ControlDevice = NULL;
        return status;
    }

    WDF_IO_QUEUE_CONFIG qcfg;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
    qcfg.EvtIoDeviceControl = KbdLayEvtIoControlDeviceControl;

    WDFQUEUE queue = NULL;
    status = WdfIoQueueCreate(g_ControlDevice, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
    {
        WdfObjectDelete(g_ControlDevice);
        g_ControlDevice = NULL;
        return status;
    }

    WdfControlFinishInitializing(g_ControlDevice);
    return STATUS_SUCCESS;
}

VOID KbdLayDeviceListAdd(_In_ WDFDEVICE Device)
{
    if (!g_DeviceListLock)
        return;

    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(Device);
    WdfSpinLockAcquire(g_DeviceListLock);
    if (!ctx->Listed)
    {
        InsertTailList(&g_DeviceList, &ctx->ListEntry);
        ctx->Listed = TRUE;
    }
    WdfSpinLockRelease(g_DeviceListLock);
}

VOID KbdLayDeviceListRemove(_In_ WDFDEVICE Device)
{
    if (!g_DeviceListLock)
        return;

    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(Device);
    WdfSpinLockAcquire(g_DeviceListLock);
    if (ctx->Listed)
    {
        RemoveEntryList(&ctx->ListEntry);
        InitializeListHead(&ctx->ListEntry);
        ctx->Listed = FALSE;
    }
    WdfSpinLockRelease(g_DeviceListLock);
}
