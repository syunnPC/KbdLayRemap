#include "KeyboardConnect.h"
#include "RemapEngine.h"

static NTSTATUS KbdLayForwardSynchronously(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target);

NTSTATUS
KbdLayHandleInternalIoctl(
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

    if (IoControlCode == IOCTL_INTERNAL_KEYBOARD_CONNECT)
    {
        if (ctx->UpperConnectValid)
        {
            WdfRequestComplete(Request, STATUS_SHARING_VIOLATION);
            return STATUS_SHARING_VIOLATION;
        }

        CONNECT_DATA* cd = NULL;
        size_t cb = 0;
        NTSTATUS status = WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA), (PVOID*)&cd, &cb);
        if (!NT_SUCCESS(status))
        {
            WdfRequestComplete(Request, status);
            return status;
        }

        // Save original class connect info locally, substitute ours (Kbfiltr pattern).
        // We only publish ctx->UpperConnect after the request succeeds.
        CONNECT_DATA upper = *cd;

        cd->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(device);
        // CONNECT_DATA.ClassService is declared as PVOID (kbdmou.h). Avoid C4152 by
        // converting via integer type.
        cd->ClassService = (PVOID)(ULONG_PTR)KbdLayClassServiceCallback;

        // Forward to lower stack. Do NOT complete the request here if send succeeds;
        // the lower target completes it.
        status = KbdLayForwardSynchronously(Request, WdfDeviceGetIoTarget(device));

        if (NT_SUCCESS(status))
        {
            WdfSpinLockAcquire(ctx->Lock);
            ctx->UpperConnect = upper;
            ctx->UpperConnectValid = TRUE;
            WdfSpinLockRelease(ctx->Lock);
        }

        return status;
    }

    // pass-through for other internal IOCTLs
    return KbdLayForwardSynchronously(Request, WdfDeviceGetIoTarget(device));
}

static NTSTATUS
KbdLayForwardSynchronously(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target)
{
    WDF_REQUEST_SEND_OPTIONS opt;
    WDF_REQUEST_SEND_OPTIONS_INIT(&opt, WDF_REQUEST_SEND_OPTION_SYNCHRONOUS);

    if (!WdfRequestSend(Request, Target, &opt))
    {
        // Request was NOT sent, so we still own it and must complete.
        NTSTATUS st = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, st);
        return st;
    }

    // Sent successfully. In synchronous mode, this returns after the lower driver
    // completes the request.
    return WdfRequestGetStatus(Request);
}

VOID
KbdLayClassServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PKEYBOARD_INPUT_DATA InputDataStart,
    _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed)
{
    // DeviceObject is our filter's WDM device object (we substituted it).
    WDFDEVICE device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    if (device == NULL)
        return;

    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(device);

    CONNECT_DATA upper;
    BOOLEAN upperValid = FALSE;

    WdfSpinLockAcquire(ctx->Lock);
    upper = ctx->UpperConnect;
    upperValid = ctx->UpperConnectValid;
    WdfSpinLockRelease(ctx->Lock);

    if (!upperValid || upper.ClassService == NULL || upper.ClassDeviceObject == NULL)
        return;

    PSERVICE_CALLBACK_ROUTINE upperCb = (PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)upper.ClassService;

    ULONG originalCount = (ULONG)(InputDataEnd - InputDataStart);
    if (originalCount == 0)
    {
        *InputDataConsumed = 0;
        return;
    }

    // Expand buffer: worst-case 3 outputs per input (shift toggle + key + restore)
    // Keep it small; if too big, bypass.
    enum { KBLAY_MAX_OUT = 256 };
    KEYBOARD_INPUT_DATA out[KBLAY_MAX_OUT];
    ULONG outCount = 0;

    for (PKEYBOARD_INPUT_DATA p = InputDataStart; p < InputDataEnd; ++p)
    {
        KEYBOARD_INPUT_DATA tmp[3];
        size_t produced = 0;
        BOOLEAN didRemap = FALSE;

        produced = KbdLayRemapOne(ctx, p, tmp, 3, &didRemap);

        if (produced == 0 || (outCount + (ULONG)produced) > KBLAY_MAX_OUT)
        {
            // Fallback: bypass entire batch
            upperCb(upper.ClassDeviceObject, InputDataStart, InputDataEnd, InputDataConsumed);
            *InputDataConsumed = originalCount;
            return;
        }

        for (size_t i = 0; i < produced; ++i)
            out[outCount++] = tmp[i];
    }

    // Call upper with our buffer. Consumption must reflect ORIGINAL input count.
    ULONG dummy = 0;
    upperCb(upper.ClassDeviceObject, out, out + outCount, &dummy);
    *InputDataConsumed = originalCount;
}
