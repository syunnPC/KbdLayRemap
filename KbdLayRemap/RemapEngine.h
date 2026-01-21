#pragma once
#include "Device.h"

VOID KbdLayRemapInit(_Inout_ PKBDLAY_DEVICE_CONTEXT Ctx);

NTSTATUS KbdLayRemapLoadRuleBlob(
    _Inout_ PKBDLAY_DEVICE_CONTEXT Ctx,
    _In_reads_bytes_(BlobSize) const VOID* Blob,
    _In_ size_t BlobSize);

size_t KbdLayRemapOne(
    _Inout_ PKBDLAY_DEVICE_CONTEXT Ctx,
    _In_ const KEYBOARD_INPUT_DATA* In,
    _Out_writes_(OutCap) KEYBOARD_INPUT_DATA* Out,
    _In_ size_t OutCap,
    _Out_ BOOLEAN* DidRemap);
