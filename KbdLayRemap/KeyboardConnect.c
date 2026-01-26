#include "KeyboardConnect.h"
#include "RemapEngine.h"

// Forward helpers.
static NTSTATUS KbdLayForwardSendAndForget(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target);

static VOID
KbdLayClearUpperConnectLocked(_Inout_ PKBDLAY_DEVICE_CONTEXT Ctx)
{
    Ctx->UpperConnectValid = FALSE;
    RtlZeroMemory(&Ctx->UpperConnect, sizeof(Ctx->UpperConnect));
}

static VOID
KbdLayConnectCompletionRoutine(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    _In_ WDFCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Target);

    PKBDLAY_DEVICE_CONTEXT ctx = (PKBDLAY_DEVICE_CONTEXT)Context;
    NTSTATUS status = CompletionParams->IoStatus.Status;

    if (ctx != NULL)
    {
        WdfSpinLockAcquire(ctx->Lock);
        if (!NT_SUCCESS(status))
            KbdLayClearUpperConnectLocked(ctx);
        WdfSpinLockRelease(ctx->Lock);
    }

    // We own completion when we set a completion routine.
    WdfRequestComplete(Request, status);
}

static VOID
KbdLayDisconnectCompletionRoutine(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    _In_ WDFCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Target);

    PKBDLAY_DEVICE_CONTEXT ctx = (PKBDLAY_DEVICE_CONTEXT)Context;
    NTSTATUS status = CompletionParams->IoStatus.Status;

    if (ctx != NULL)
    {
        WdfSpinLockAcquire(ctx->Lock);
        if (NT_SUCCESS(status))
            KbdLayClearUpperConnectLocked(ctx);
        WdfSpinLockRelease(ctx->Lock);
    }

    // We own completion when we set a completion routine.
    WdfRequestComplete(Request, status);
}

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
    WDFIOTARGET target = WdfDeviceGetIoTarget(device);

    if (IoControlCode == IOCTL_INTERNAL_KEYBOARD_CONNECT)
    {
        // Only allow one connection.
        WdfSpinLockAcquire(ctx->Lock);
        BOOLEAN alreadyConnected = ctx->UpperConnectValid ? TRUE : FALSE;
        WdfSpinLockRelease(ctx->Lock);

        if (alreadyConnected)
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

        // Cache original connect info so our callback can call the next driver.
        WdfSpinLockAcquire(ctx->Lock);
        ctx->UpperConnect = *cd;
        ctx->UpperConnectValid = TRUE;
        WdfSpinLockRelease(ctx->Lock);

        // Hook into the report chain.
        cd->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(device);

#pragma warning(push)
#pragma warning(disable : 4152) // function/data pointer conversion for CONNECT_DATA.ClassService
        cd->ClassService = (PVOID)(ULONG_PTR)KbdLayClassServiceCallback;
#pragma warning(pop)

        // Forward down and complete in our completion routine.
        // NOTE: WdfRequestFormatRequestUsingCurrentType returns VOID.
        WdfRequestFormatRequestUsingCurrentType(Request);
        WdfRequestSetCompletionRoutine(Request, KbdLayConnectCompletionRoutine, (WDFCONTEXT)ctx);

        if (!WdfRequestSend(Request, target, WDF_NO_SEND_OPTIONS))
        {
            // Request NOT sent => we still own it and must complete.
            status = WdfRequestGetStatus(Request);

            // Roll back cached state to allow retry.
            WdfSpinLockAcquire(ctx->Lock);
            KbdLayClearUpperConnectLocked(ctx);
            WdfSpinLockRelease(ctx->Lock);

            WdfRequestComplete(Request, status);
            return status;
        }

        return STATUS_PENDING;
    }
    else if (IoControlCode == IOCTL_INTERNAL_KEYBOARD_DISCONNECT)
    {
        // Lower stack is disconnecting; drop cached connect state on success.
        // Forward down and complete in our completion routine.
        // NOTE: WdfRequestFormatRequestUsingCurrentType returns VOID.
        WdfRequestFormatRequestUsingCurrentType(Request);
        WdfRequestSetCompletionRoutine(Request, KbdLayDisconnectCompletionRoutine, (WDFCONTEXT)ctx);

        if (!WdfRequestSend(Request, target, WDF_NO_SEND_OPTIONS))
        {
            NTSTATUS status = WdfRequestGetStatus(Request);

            WdfRequestComplete(Request, status);
            return status;
        }

        return STATUS_PENDING;
    }

    // Pass-through for all other internal IOCTLs.
    return KbdLayForwardSendAndForget(Request, target);
}

static NTSTATUS
KbdLayForwardSendAndForget(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target)
{
    // NOTE: WdfRequestFormatRequestUsingCurrentType returns VOID.
    WdfRequestFormatRequestUsingCurrentType(Request);

    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    if (!WdfRequestSend(Request, Target, &options))
    {
        NTSTATUS st = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, st);
        return st;
    }

    return STATUS_PENDING;
}

VOID
KbdLayClassServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PKEYBOARD_INPUT_DATA InputDataStart,
    _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed)
{
    // DeviceObject is our filter's WDM device object (we substituted it in CONNECT_DATA).
    WDFDEVICE device = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
    if (device == NULL)
    {
        if (InputDataConsumed) *InputDataConsumed = 0;
        return;
    }

    PKBDLAY_DEVICE_CONTEXT ctx = KbdLayGetDeviceContext(device);

    CONNECT_DATA upper;
    BOOLEAN upperValid = FALSE;

    WdfSpinLockAcquire(ctx->Lock);
    upper = ctx->UpperConnect;
    upperValid = ctx->UpperConnectValid;
    WdfSpinLockRelease(ctx->Lock);

    if (!upperValid || upper.ClassService == NULL || upper.ClassDeviceObject == NULL)
    {
        if (InputDataConsumed && InputDataEnd >= InputDataStart)
            *InputDataConsumed = (ULONG)(InputDataEnd - InputDataStart);
        return;
    }

    PSERVICE_CALLBACK_ROUTINE upperCb = (PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)upper.ClassService;

    if (InputDataEnd < InputDataStart)
    {
        if (InputDataConsumed) *InputDataConsumed = 0;
        return;
    }

    ULONG originalCount = (ULONG)(InputDataEnd - InputDataStart);
    if (originalCount == 0)
    {
        if (InputDataConsumed) *InputDataConsumed = 0;
        return;
    }

    ULONG inputConsumed = 0;
    BOOLEAN upperLost = FALSE;

    for (PKEYBOARD_INPUT_DATA p = InputDataStart; p < InputDataEnd; ++p)
    {
        // If we get disconnected mid-callback, stop calling the upper driver.
        WdfSpinLockAcquire(ctx->Lock);
        BOOLEAN stillValid = ctx->UpperConnectValid ? TRUE : FALSE;
        WdfSpinLockRelease(ctx->Lock);

        if (!stillValid)
        {
            upperLost = TRUE;
            break;
        }

        KEYBOARD_INPUT_DATA tmp[3];
        BOOLEAN didRemap = FALSE;

        size_t produced = KbdLayRemapOne(ctx, p, tmp, RTL_NUMBER_OF(tmp), &didRemap);

        if (produced == 0)
        {
            ULONG consumedOut = 0;
            upperCb(upper.ClassDeviceObject, p, p + 1, &consumedOut);
            if (consumedOut == 0)
                break;

            inputConsumed += 1;
            continue;
        }

        ULONG producedOut = (ULONG)produced;
        ULONG consumedOut = 0;
        upperCb(upper.ClassDeviceObject, tmp, tmp + producedOut, &consumedOut);
        if (consumedOut < producedOut)
            break;

        inputConsumed += 1;
    }

    if (InputDataConsumed)
        *InputDataConsumed = upperLost ? originalCount : inputConsumed;
}
