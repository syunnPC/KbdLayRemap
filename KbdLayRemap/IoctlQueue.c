#include "Device.h"
#include "KeyboardConnect.h"
#include "RemapEngine.h"

static __forceinline BOOLEAN KbdLayIsValidRole(_In_ UINT32 Role)
{
    return (Role == (UINT32)KBLAY_ROLE_NONE || Role == (UINT32)KBLAY_ROLE_BASE || Role == (UINT32)KBLAY_ROLE_REMAP) ? TRUE : FALSE;
}

static __forceinline BOOLEAN KbdLayIsValidState(_In_ UINT32 State)
{
    return (State == (UINT32)KBLAY_STATE_BYPASS_HARD || State == (UINT32)KBLAY_STATE_BYPASS_SOFT || State == (UINT32)KBLAY_STATE_ACTIVE) ? TRUE : FALSE;
}

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
            if (!KbdLayIsValidRole(in->Role))
            {
                status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                InterlockedExchange(&ctx->Role, (LONG)in->Role);
                status = STATUS_SUCCESS;
            }
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_SET_STATE)
    {
        KBLAY_SET_STATE_INPUT* in = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_SET_STATE_INPUT), (PVOID*)&in, &cb);
        if (NT_SUCCESS(status))
        {
            if (!KbdLayIsValidState(in->State))
            {
                status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                InterlockedExchange(&ctx->State, (LONG)in->State);
                status = STATUS_SUCCESS;
            }
        }
    }
    else if (IoControlCode == IOCTL_KBLAY_SET_RULE_BLOB)
    {
        VOID* blob = NULL;
        size_t cb = 0;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(KBLAY_RULE_BLOB_HEADER), &blob, &cb);
        if (NT_SUCCESS(status))
        {
            if (cb == 0 || cb > (size_t)KBLAY_MAX_RULE_BLOB_BYTES)
            {
                status = STATUS_INVALID_BUFFER_SIZE;
            }
            else
            {
                status = KbdLayRemapLoadRuleBlob(ctx, blob, cb);
                if (!NT_SUCCESS(status))
                    InterlockedExchange(&ctx->LastErrorNtStatus, (LONG)status);
            }
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

            // Snapshot fields atomically (avoid torn reads on 32-bit targets and reduce inconsistency).
            out->Role = (UINT32)InterlockedCompareExchange((volatile LONG*)&ctx->Role, 0, 0);
            out->State = (UINT32)InterlockedCompareExchange((volatile LONG*)&ctx->State, 0, 0);

            out->RemapHitCount = (UINT64)InterlockedCompareExchange64((volatile LONG64*)&ctx->RemapHitCount, 0, 0);
            out->PassThroughCount = (UINT64)InterlockedCompareExchange64((volatile LONG64*)&ctx->PassThroughCount, 0, 0);
            out->UnmappedCount = (UINT64)InterlockedCompareExchange64((volatile LONG64*)&ctx->UnmappedCount, 0, 0);
            out->ShiftToggleCount = (UINT64)InterlockedCompareExchange64((volatile LONG64*)&ctx->ShiftToggleCount, 0, 0);

            out->LastErrorNtStatus = (UINT32)InterlockedCompareExchange((volatile LONG*)&ctx->LastErrorNtStatus, 0, 0);

            WdfSpinLockAcquire(ctx->Lock);
            out->ContainerId = ctx->ContainerId;
            WdfSpinLockRelease(ctx->Lock);

            WdfRequestSetInformation(Request, sizeof(*out));
            status = STATUS_SUCCESS;
        }
    }
    else
    {
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    WdfRequestComplete(Request, status);
}
