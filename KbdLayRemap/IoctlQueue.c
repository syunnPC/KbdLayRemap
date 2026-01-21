#include "Device.h"
#include "KeyboardConnect.h"
#include "RemapEngine.h"

VOID
KbdLayEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    (VOID)KbdLayHandleInternalIoctl(Queue, Request, OutputBufferLength, InputBufferLength, IoControlCode);
}

VOID
KbdLayEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(device);

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    if (IoControlCode == IOCTL_KBLAY_SET_ROLE)
    {
        KBLAY_SET_ROLE_INPUT* in = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_SET_ROLE_INPUT), (PVOID*)&in, &cb);
        if (NT_SUCCESS(status))
        {
            InterlockedExchange(&ctx->Role, (LONG)in->Role);
            status = STATUS_SUCCESS;
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_SET_STATE)
    {
        KBLAY_SET_STATE_INPUT* in = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_SET_STATE_INPUT), (PVOID*)&in, &cb);
        if (NT_SUCCESS(status))
        {
            InterlockedExchange(&ctx->State, (LONG)in->State);
            status = STATUS_SUCCESS;
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_SET_RULE_BLOB)
    {
        VOID* blob = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_RULE_BLOB_HEADER), &blob, &cb);
        if (NT_SUCCESS(status))
        {
            status = KbdLayRemapLoadRuleBlob(ctx, blob, cb);
            if (!NT_SUCCESS(status))
                InterlockedExchange(&ctx->LastErrorNtStatus, (LONG)status);
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_GET_STATUS)
    {
        KBLAY_STATUS_OUTPUT* out = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(KBLAY_STATUS_OUTPUT), (PVOID*)&out, &cb);
        if (NT_SUCCESS(status))
        {
            RtlZeroMemory(out, sizeof(*out));
            out->Role = (UINT32)ctx->Role;
            out->State = (UINT32)ctx->State;

            out->RemapHitCount = (UINT64)ctx->RemapHitCount;
            out->PassThroughCount = (UINT64)ctx->PassThroughCount;
            out->UnmappedCount = (UINT64)ctx->UnmappedCount;
            out->ShiftToggleCount = (UINT64)ctx->ShiftToggleCount;

            out->LastErrorNtStatus = (UINT32)ctx->LastErrorNtStatus;

            WdfSpinLockAcquire(ctx->Lock);
            out->ContainerId = ctx->ContainerId;
            WdfSpinLockRelease(ctx->Lock);

            WdfRequestSetInformation(Request, sizeof(*out));
            status = STATUS_SUCCESS;
        }
    }

    WdfRequestComplete(Request, status);
}
