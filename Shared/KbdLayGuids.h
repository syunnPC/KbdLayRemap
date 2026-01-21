#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <guiddef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

	// Device Interface GUID (filter instance enumeration)
	// {7A9B3B9E-7B3A-4A7E-9D7D-68E1D9A4E2B1}
	static const GUID GUID_DEVINTERFACE_KbdLayRemap =
	{ 0x7a9b3b9e, 0x7b3a, 0x4a7e, { 0x9d, 0x7d, 0x68, 0xe1, 0xd9, 0xa4, 0xe2, 0xb1 } };

#ifdef __cplusplus
}
#endif
