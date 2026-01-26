#pragma once
#include "KbdLayGuids.h"
#include "KbdLayRules.h"
#include "KbdLayIoctl.h"

#ifndef KBLAY_CONTROL_DEVICE_NT_NAME
#define KBLAY_CONTROL_DEVICE_NT_NAME L"\\Device\\KbdLayRemap"
#endif

#ifndef KBLAY_CONTROL_DEVICE_SYMBOLIC_LINK
#define KBLAY_CONTROL_DEVICE_SYMBOLIC_LINK L"\\DosDevices\\KbdLayRemap"
#endif

#ifndef KBLAY_CONTROL_DEVICE_DOS_NAME
#define KBLAY_CONTROL_DEVICE_DOS_NAME L"\\\\.\\KbdLayRemap"
#endif

#ifndef KBLAY_MAX_RULE_ENTRIES
#define KBLAY_MAX_RULE_ENTRIES      1024u
#endif

#ifndef KBLAY_MAX_RULE_BLOB_BYTES
#define KBLAY_MAX_RULE_BLOB_BYTES   (64u * 1024u)
#endif
