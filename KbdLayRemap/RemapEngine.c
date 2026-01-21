#include "RemapEngine.h"

static BOOLEAN IsKeyBreak(_In_ const KEYBOARD_INPUT_DATA* k) { return (k->Flags & KEY_BREAK) != 0; }
static BOOLEAN IsKeyE0(_In_ const KEYBOARD_INPUT_DATA* k) { return (k->Flags & KEY_E0) != 0; }
static BOOLEAN IsKeyE1(_In_ const KEYBOARD_INPUT_DATA* k) { return (k->Flags & KEY_E1) != 0; }

static BOOLEAN IsShiftMakeCode(_In_ USHORT mc) { return (mc == 0x2A || mc == 0x36); } // LSHIFT/RSHIFT
static BOOLEAN IsCtrlMakeCode(_In_ USHORT mc, _In_ USHORT flags) { UNREFERENCED_PARAMETER(flags); return (mc == 0x1D); }
static BOOLEAN IsAltMakeCode(_In_ USHORT mc) { return (mc == 0x38); }
static BOOLEAN IsWinMakeCodeE0(_In_ USHORT mc, _In_ USHORT flags) { return ((flags & KEY_E0) && (mc == 0x5B || mc == 0x5C)); }

static VOID SetLastError(_Inout_ PKBDLAY_DEVICE_CONTEXT ctx, _In_ NTSTATUS st)
{
    InterlockedExchange(&ctx->LastErrorNtStatus, (LONG)st);
}

VOID
KbdLayRemapInit(_Inout_ PKBDLAY_DEVICE_CONTEXT Ctx)
{
    // default: no rules (all invalid)
    for (int e0 = 0; e0 < 2; ++e0)
        for (int sh = 0; sh < 2; ++sh)
            for (int sc = 0; sc < 256; ++sc)
                Ctx->RuleValid[e0][sh][sc] = FALSE;

    Ctx->PhysShift = FALSE;
    Ctx->PhysCtrl = FALSE;
    Ctx->PhysAlt = FALSE;
    Ctx->PhysWin = FALSE;
}

NTSTATUS
KbdLayRemapLoadRuleBlob(
    _Inout_ PKBDLAY_DEVICE_CONTEXT Ctx,
    _In_reads_bytes_(BlobSize) const VOID* Blob,
    _In_ size_t BlobSize)
{
    if (Blob == NULL || BlobSize < sizeof(KBLAY_RULE_BLOB_HEADER))
        return STATUS_INVALID_PARAMETER;

    const KBLAY_RULE_BLOB_HEADER* h = (const KBLAY_RULE_BLOB_HEADER*)Blob;
    if (h->Version != KBLAY_RULE_BLOB_VERSION || h->Reserved != 0)
        return STATUS_INVALID_PARAMETER;

    if (h->TotalSizeBytes != (UINT32)BlobSize)
        return STATUS_INVALID_PARAMETER;

    size_t need = sizeof(KBLAY_RULE_BLOB_HEADER) + (size_t)h->EntryCount * sizeof(KBLAY_RULE_ENTRY);
    if (need != BlobSize)
        return STATUS_INVALID_PARAMETER;

    const KBLAY_RULE_ENTRY* e = (const KBLAY_RULE_ENTRY*)((const UINT8*)Blob + sizeof(KBLAY_RULE_BLOB_HEADER));

    WdfSpinLockAcquire(Ctx->Lock);

    // clear
    for (int e0 = 0; e0 < 2; ++e0)
        for (int sh = 0; sh < 2; ++sh)
            for (int sc = 0; sc < 256; ++sc)
                Ctx->RuleValid[e0][sh][sc] = FALSE;

    for (UINT32 i = 0; i < h->EntryCount; ++i)
    {
        UINT8 inE0 = (e[i].InFlags & KBLAY_FLAG_E0) ? 1 : 0;
        UINT8 inSh = (e[i].InFlags & KBLAY_FLAG_SHIFT) ? 1 : 0;

        Ctx->Rules[inE0][inSh][e[i].InMakeCode].OutMakeCode = e[i].OutMakeCode;
        Ctx->Rules[inE0][inSh][e[i].InMakeCode].OutFlags = e[i].OutFlags;
        Ctx->RuleValid[inE0][inSh][e[i].InMakeCode] = TRUE;
    }

    WdfSpinLockRelease(Ctx->Lock);
    return STATUS_SUCCESS;
}

static VOID UpdatePhysicalMods(_Inout_ PKBDLAY_DEVICE_CONTEXT ctx, _In_ const KEYBOARD_INPUT_DATA* in)
{
    const BOOLEAN isBreak = IsKeyBreak(in);
    const USHORT mc = in->MakeCode;
    const USHORT fl = in->Flags;

    if (IsShiftMakeCode(mc))
        ctx->PhysShift = !isBreak;
    else if (IsCtrlMakeCode(mc, fl))
        ctx->PhysCtrl = !isBreak;
    else if (IsAltMakeCode(mc))
        ctx->PhysAlt = !isBreak;
    else if (IsWinMakeCodeE0(mc, fl))
        ctx->PhysWin = !isBreak;
}

static size_t EmitShiftToggle(_In_ BOOLEAN shiftDown, _Out_ KEYBOARD_INPUT_DATA* out, _In_ size_t cap)
{
    if (cap < 1) return 0;
    KEYBOARD_INPUT_DATA k = { 0 };
    k.UnitId = 0;
    k.MakeCode = 0x2A; // LSHIFT
    k.Flags = shiftDown ? KEY_MAKE : KEY_BREAK;
    out[0] = k;
    return 1;
}

size_t
KbdLayRemapOne(
    _Inout_ PKBDLAY_DEVICE_CONTEXT Ctx,
    _In_ const KEYBOARD_INPUT_DATA* In,
    _Out_writes_(OutCap) KEYBOARD_INPUT_DATA* Out,
    _In_ size_t OutCap,
    _Out_ BOOLEAN* DidRemap)
{
    *DidRemap = FALSE;

    if (OutCap < 1 || In == NULL || Out == NULL)
        return 0;

    if (IsKeyE1(In))
    {
        Out[0] = *In;
        InterlockedIncrement64(&Ctx->PassThroughCount);
        return 1;
    }

    // update physical modifier states from hardware events
    WdfSpinLockAcquire(Ctx->Lock);
    UpdatePhysicalMods(Ctx, In);
    const BOOLEAN physShift = Ctx->PhysShift;
    const BOOLEAN physCtrl = Ctx->PhysCtrl;
    const BOOLEAN physAlt = Ctx->PhysAlt;
    const BOOLEAN physWin = Ctx->PhysWin;
    const LONG role = Ctx->Role;
    const LONG state = Ctx->State;
    WdfSpinLockRelease(Ctx->Lock);

    // bypass conditions
    if (state != KBLAY_STATE_ACTIVE || role != KBLAY_ROLE_REMAP || physCtrl || physAlt || physWin)
    {
        Out[0] = *In;
        InterlockedIncrement64(&Ctx->PassThroughCount);
        return 1;
    }

    // Do not remap pure modifier keys themselves
    if (IsShiftMakeCode(In->MakeCode) || IsCtrlMakeCode(In->MakeCode, In->Flags) || IsAltMakeCode(In->MakeCode) || IsWinMakeCodeE0(In->MakeCode, In->Flags))
    {
        Out[0] = *In;
        InterlockedIncrement64(&Ctx->PassThroughCount);
        return 1;
    }

    const UINT8 inE0 = IsKeyE0(In) ? 1 : 0;
    const UINT8 inSh = physShift ? 1 : 0;

    BOOLEAN valid = FALSE;
    KBLAY_RULE_CELL cell;

    WdfSpinLockAcquire(Ctx->Lock);
    valid = Ctx->RuleValid[inE0][inSh][(UINT8)In->MakeCode];
    cell = Ctx->Rules[inE0][inSh][(UINT8)In->MakeCode];
    WdfSpinLockRelease(Ctx->Lock);

    if (!valid)
    {
        Out[0] = *In;
        InterlockedIncrement64(&Ctx->UnmappedCount);
        return 1;
    }

    // build mapped key event
    KEYBOARD_INPUT_DATA mapped = *In;
    mapped.MakeCode = cell.OutMakeCode;

    // preserve MAKE/BREAK, override E0 according to OutFlags, drop E1
    USHORT newFlags = (In->Flags & (KEY_MAKE | KEY_BREAK));
    if (cell.OutFlags & KBLAY_FLAG_E0) newFlags |= KEY_E0;
    mapped.Flags = newFlags;

    const BOOLEAN outDesiredShift = (cell.OutFlags & KBLAY_FLAG_SHIFT) ? TRUE : FALSE;

    // For BREAK, we do not do shift toggles; just send mapped break.
    if (IsKeyBreak(In))
    {
        Out[0] = mapped;
        *DidRemap = TRUE;
        InterlockedIncrement64(&Ctx->RemapHitCount);
        return 1;
    }

    // For MAKE, if desired shift differs from physical shift, temporarily toggle shift around the MAKE.
    if (outDesiredShift != physShift)
    {
        if (OutCap < 3)
        {
            // insufficient buffer -> pass-through
            Out[0] = *In;
            SetLastError(Ctx, STATUS_BUFFER_TOO_SMALL);
            InterlockedIncrement64(&Ctx->PassThroughCount);
            return 1;
        }

        size_t n = 0;
        n += EmitShiftToggle(outDesiredShift, &Out[n], OutCap - n);
        Out[n++] = mapped;
        n += EmitShiftToggle(physShift, &Out[n], OutCap - n);

        *DidRemap = TRUE;
        InterlockedIncrement64(&Ctx->RemapHitCount);
        InterlockedIncrement64(&Ctx->ShiftToggleCount);
        return n;
    }

    Out[0] = mapped;
    *DidRemap = TRUE;
    InterlockedIncrement64(&Ctx->RemapHitCount);
    return 1;
}
