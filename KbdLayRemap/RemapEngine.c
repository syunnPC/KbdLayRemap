#include "RemapEngine.h"

#define KBLAY_POOL_TAG_RULES 'rLbK'

// If Public.h does not define these yet, provide safe defaults.
#ifndef KBLAY_MAX_RULE_ENTRIES
#define KBLAY_MAX_RULE_ENTRIES      1024u
#endif

#ifndef KBLAY_MAX_RULE_BLOB_BYTES
#define KBLAY_MAX_RULE_BLOB_BYTES   (64u * 1024u)
#endif

// Common set-1 make codes for modifiers (no E0 for shifts).
#define KBLAY_MAKE_LSHIFT 0x2A
#define KBLAY_MAKE_RSHIFT 0x36
#define KBLAY_MAKE_CTRL   0x1D
#define KBLAY_MAKE_ALT    0x38
#define KBLAY_MAKE_LWIN   0x5B
#define KBLAY_MAKE_RWIN   0x5C

typedef struct _KBLAY_RULE_TABLE
{
    KBLAY_RULE_CELL Rules[2][2][256];
    BOOLEAN         RuleValid[2][2][256];
} KBLAY_RULE_TABLE;

static __forceinline BOOLEAN IsKeyBreak(_In_ const KEYBOARD_INPUT_DATA* In)
{
    return (In->Flags & KEY_BREAK) ? TRUE : FALSE;
}

static __forceinline BOOLEAN IsE0(_In_ const KEYBOARD_INPUT_DATA* In)
{
    return (In->Flags & KEY_E0) ? TRUE : FALSE;
}

static __forceinline BOOLEAN IsE1(_In_ const KEYBOARD_INPUT_DATA* In)
{
    return (In->Flags & KEY_E1) ? TRUE : FALSE;
}

static __forceinline BOOLEAN IsShiftMakeCode(_In_ USHORT MakeCode)
{
    return (MakeCode == KBLAY_MAKE_LSHIFT || MakeCode == KBLAY_MAKE_RSHIFT) ? TRUE : FALSE;
}

static VOID UpdatePhysicalMods(_Inout_ PKBDLAY_DEVICE_CONTEXT Ctx, _In_ const KEYBOARD_INPUT_DATA* In)
{
    const BOOLEAN brk = IsKeyBreak(In);
    const BOOLEAN e0 = IsE0(In);
    const USHORT mc = In->MakeCode;

    // Shift (set-1; no E0)
    if (!e0 && mc == KBLAY_MAKE_LSHIFT)
    {
        Ctx->PhysLShift = brk ? FALSE : TRUE;
        return;
    }
    if (!e0 && mc == KBLAY_MAKE_RSHIFT)
    {
        Ctx->PhysRShift = brk ? FALSE : TRUE;
        return;
    }

    // Ctrl (E0 distinguishes right)
    if (mc == KBLAY_MAKE_CTRL)
    {
        if (e0) Ctx->PhysRCtrl = brk ? FALSE : TRUE;
        else    Ctx->PhysLCtrl = brk ? FALSE : TRUE;
        return;
    }

    // Alt (E0 distinguishes right alt / AltGr)
    if (mc == KBLAY_MAKE_ALT)
    {
        if (e0) Ctx->PhysRAlt = brk ? FALSE : TRUE;
        else    Ctx->PhysLAlt = brk ? FALSE : TRUE;
        return;
    }

    // Win (typically E0, but be permissive)
    if (mc == KBLAY_MAKE_LWIN)
    {
        Ctx->PhysLWin = brk ? FALSE : TRUE;
        return;
    }
    if (mc == KBLAY_MAKE_RWIN)
    {
        Ctx->PhysRWin = brk ? FALSE : TRUE;
        return;
    }
}

static VOID MakeSyntheticShift(
    _Out_ KEYBOARD_INPUT_DATA* Out,
    _In_ const KEYBOARD_INPUT_DATA* Ref,
    _In_ BOOLEAN Down)
{
    *Out = *Ref;
    Out->MakeCode = KBLAY_MAKE_LSHIFT; // choose left shift for synthesis
    Out->Flags = Ref->Flags;

    // Ensure no extended flags on synthetic shift.
    Out->Flags &= ~(KEY_E0 | KEY_E1);

    if (Down) Out->Flags &= ~KEY_BREAK;
    else      Out->Flags |= KEY_BREAK;
}

VOID KbdLayRemapInit(_Inout_ PKBDLAY_DEVICE_CONTEXT Ctx)
{
    // Modifier states (physical)
    Ctx->PhysLShift = FALSE;
    Ctx->PhysRShift = FALSE;
    Ctx->PhysLCtrl = FALSE;
    Ctx->PhysRCtrl = FALSE;
    Ctx->PhysLAlt = FALSE;
    Ctx->PhysRAlt = FALSE;
    Ctx->PhysLWin = FALSE;
    Ctx->PhysRWin = FALSE;

    // Rules
    RtlZeroMemory(Ctx->Rules, sizeof(Ctx->Rules));
    RtlZeroMemory(Ctx->RuleValid, sizeof(Ctx->RuleValid));
}

NTSTATUS KbdLayRemapLoadRuleBlob(
    _Inout_ PKBDLAY_DEVICE_CONTEXT Ctx,
    _In_reads_bytes_(BlobSize) const VOID* Blob,
    _In_ size_t BlobSize)
{
    if (Blob == NULL)
        return STATUS_INVALID_PARAMETER;

    if (BlobSize < sizeof(KBLAY_RULE_BLOB_HEADER) || BlobSize >(size_t)KBLAY_MAX_RULE_BLOB_BYTES)
        return STATUS_INVALID_PARAMETER;

    const KBLAY_RULE_BLOB_HEADER* h = (const KBLAY_RULE_BLOB_HEADER*)Blob;

    // Expect: Version / Reserved / TotalSizeBytes / EntryCount
    if (h->Version != KBLAY_RULE_BLOB_VERSION || h->Reserved != 0)
        return STATUS_INVALID_PARAMETER;

    if (h->TotalSizeBytes != (UINT32)BlobSize)
        return STATUS_INVALID_PARAMETER;

    if (h->EntryCount > KBLAY_MAX_RULE_ENTRIES)
        return STATUS_INVALID_PARAMETER;

    const size_t headerBytes = sizeof(KBLAY_RULE_BLOB_HEADER);
    const size_t entryBytes = sizeof(KBLAY_RULE_ENTRY);

    if (BlobSize < headerBytes)
        return STATUS_INVALID_PARAMETER;

    const size_t maxEntriesBySize = (BlobSize - headerBytes) / entryBytes;
    if ((size_t)h->EntryCount > maxEntriesBySize)
        return STATUS_INVALID_PARAMETER;

    const size_t need = headerBytes + (size_t)h->EntryCount * entryBytes;
    if (need != BlobSize)
        return STATUS_INVALID_PARAMETER;

    const KBLAY_RULE_ENTRY* e = (const KBLAY_RULE_ENTRY*)((const UINT8*)Blob + headerBytes);

    // Build table outside the spin lock
    KBLAY_RULE_TABLE* tbl = (KBLAY_RULE_TABLE*)ExAllocatePoolWithTag(
        NonPagedPoolNx, sizeof(KBLAY_RULE_TABLE), KBLAY_POOL_TAG_RULES);
    if (!tbl)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(tbl, sizeof(*tbl));

    const UINT8 allowedMask = (UINT8)(KBLAY_FLAG_E0 | KBLAY_FLAG_SHIFT);

    for (UINT32 i = 0; i < h->EntryCount; ++i)
    {
        const UINT8 inFlags = (UINT8)(e[i].InFlags & allowedMask);
        const UINT8 outFlags = (UINT8)(e[i].OutFlags & allowedMask);

        const UINT8 inE0 = (inFlags & KBLAY_FLAG_E0) ? 1 : 0;
        const UINT8 inSh = (inFlags & KBLAY_FLAG_SHIFT) ? 1 : 0;

        tbl->Rules[inE0][inSh][e[i].InMakeCode].OutMakeCode = e[i].OutMakeCode;
        tbl->Rules[inE0][inSh][e[i].InMakeCode].OutFlags = outFlags;
        tbl->RuleValid[inE0][inSh][e[i].InMakeCode] = TRUE;
    }

    WdfSpinLockAcquire(Ctx->Lock);
    RtlCopyMemory(Ctx->Rules, tbl->Rules, sizeof(Ctx->Rules));
    RtlCopyMemory(Ctx->RuleValid, tbl->RuleValid, sizeof(Ctx->RuleValid));
    WdfSpinLockRelease(Ctx->Lock);

    ExFreePoolWithTag(tbl, KBLAY_POOL_TAG_RULES);
    return STATUS_SUCCESS;
}

size_t KbdLayRemapOne(
    _Inout_ PKBDLAY_DEVICE_CONTEXT Ctx,
    _In_ const KEYBOARD_INPUT_DATA* In,
    _Out_writes_(OutCap) KEYBOARD_INPUT_DATA* Out,
    _In_ size_t OutCap,
    _Out_ BOOLEAN* DidRemap)
{
    *DidRemap = FALSE;

    if (OutCap < 1)
        return 0;

    const LONG state = InterlockedCompareExchange(&Ctx->State, 0, 0);
    const LONG role = InterlockedCompareExchange(&Ctx->Role, 0, 0);

    // Hard bypass: do not touch input nor internal state.
    if (state == (LONG)KBLAY_STATE_BYPASS_HARD)
    {
        Out[0] = *In;
        InterlockedIncrement64(&Ctx->PassThroughCount);
        return 1;
    }

    // Soft bypass: pass through input, but keep tracking physical modifiers.
    if (state == (LONG)KBLAY_STATE_BYPASS_SOFT)
    {
        WdfSpinLockAcquire(Ctx->Lock);
        UpdatePhysicalMods(Ctx, In);
        WdfSpinLockRelease(Ctx->Lock);

        Out[0] = *In;
        InterlockedIncrement64(&Ctx->PassThroughCount);
        return 1;
    }

    // Active but non-remap role: treat as soft bypass for safety.
    if (state == (LONG)KBLAY_STATE_ACTIVE && role != (LONG)KBLAY_ROLE_REMAP)
    {
        WdfSpinLockAcquire(Ctx->Lock);
        UpdatePhysicalMods(Ctx, In);
        WdfSpinLockRelease(Ctx->Lock);

        Out[0] = *In;
        InterlockedIncrement64(&Ctx->PassThroughCount);
        return 1;
    }

    // ACTIVE: Update physical modifiers and snapshot shift state + rule cell under lock.
    if (In->MakeCode > 0xFF)
    {
        // Unknown/extended make codes: cannot index the rule table safely.
        // Still track physical modifiers (in case the platform ever produces such codes for modifiers).
        WdfSpinLockAcquire(Ctx->Lock);
        UpdatePhysicalMods(Ctx, In);
        WdfSpinLockRelease(Ctx->Lock);

        Out[0] = *In;
        InterlockedIncrement64(&Ctx->UnmappedCount);
        return 1;
    }

    BOOLEAN physShift = FALSE;
    BOOLEAN valid = FALSE;
    KBLAY_RULE_CELL cell = { 0 };

    const UINT8 inE0 = IsE0(In) ? 1 : 0;

    WdfSpinLockAcquire(Ctx->Lock);
    UpdatePhysicalMods(Ctx, In);
    physShift = (Ctx->PhysLShift || Ctx->PhysRShift) ? TRUE : FALSE;

    const UINT8 mc8 = (UINT8)In->MakeCode;
    const UINT8 inSh = physShift ? 1 : 0;
    valid = Ctx->RuleValid[inE0][inSh][mc8];
    if (valid)
        cell = Ctx->Rules[inE0][inSh][mc8];
    WdfSpinLockRelease(Ctx->Lock);

    if (!valid || cell.OutMakeCode == 0)
    {
        Out[0] = *In;
        InterlockedIncrement64(&Ctx->UnmappedCount);
        return 1;
    }

    // Build the remapped event
    KEYBOARD_INPUT_DATA mapped = *In;
    mapped.MakeCode = (USHORT)cell.OutMakeCode;

    // Adjust only E0 flag based on rule; preserve BREAK/E1 and any other bits.
    if (cell.OutFlags & KBLAY_FLAG_E0) mapped.Flags |= KEY_E0;
    else                              mapped.Flags &= ~KEY_E0;

    // Shift-handling policy:
    // We interpret KBLAY_FLAG_SHIFT in OutFlags as "emit the output as if Shift is held".
    const BOOLEAN outShiftWanted = (cell.OutFlags & KBLAY_FLAG_SHIFT) ? TRUE : FALSE;

    // Never synthesize shift around actual Shift key events (avoid weirdness).
    if (IsShiftMakeCode(In->MakeCode) || IsE1(In))
    {
        Out[0] = mapped;
        *DidRemap = TRUE;
        InterlockedIncrement64(&Ctx->RemapHitCount);
        return 1;
    }

    if (physShift == outShiftWanted)
    {
        Out[0] = mapped;
        *DidRemap = TRUE;
        InterlockedIncrement64(&Ctx->RemapHitCount);
        return 1;
    }

    // Need shift toggle. Require 3 slots.
    if (OutCap < 3)
    {
        // Best-effort fallback: no toggle, just emit mapped.
        Out[0] = mapped;
        *DidRemap = TRUE;
        InterlockedIncrement64(&Ctx->RemapHitCount);
        return 1;
    }

    if (outShiftWanted && !physShift)
    {
        // Shift DOWN -> key -> Shift UP
        MakeSyntheticShift(&Out[0], In, TRUE);
        Out[1] = mapped;
        MakeSyntheticShift(&Out[2], In, FALSE);
    }
    else
    {
        // physShift == TRUE && outShiftWanted == FALSE
        // Shift UP -> key -> Shift DOWN
        MakeSyntheticShift(&Out[0], In, FALSE);
        Out[1] = mapped;
        MakeSyntheticShift(&Out[2], In, TRUE);
    }

    *DidRemap = TRUE;
    InterlockedIncrement64(&Ctx->RemapHitCount);
    InterlockedIncrement64(&Ctx->ShiftToggleCount);
    return 3;
}
