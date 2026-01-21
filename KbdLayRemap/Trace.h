#pragma once
#include <ntddk.h>

#define KBDLAY_DPFLTR_ID DPFLTR_IHVDRIVER_ID

#define KbdLayLogInfo(fmt, ...)  DbgPrintEx(KBDLAY_DPFLTR_ID, DPFLTR_INFO_LEVEL,  "[KbdLay] " fmt "\n", __VA_ARGS__)
#define KbdLayLogError(fmt, ...) DbgPrintEx(KBDLAY_DPFLTR_ID, DPFLTR_ERROR_LEVEL, "[KbdLay] " fmt "\n", __VA_ARGS__)
