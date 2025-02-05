#include <clocale>
#include "Hooks.h"
#include "lib/AntiVAC/AntiDetection.h"

#ifndef NEPS_DEBUG
AntiDetection anti;
#endif

extern "C" BOOL WINAPI _CRT_INIT(HMODULE moduleHandle, DWORD reason, LPVOID reserved);

BOOL APIENTRY DllEntryPoint(HMODULE moduleHandle, DWORD reason, LPVOID reserved)
{
	if (!_CRT_INIT(moduleHandle, reason, reserved))
		return FALSE;

	if (reason == DLL_PROCESS_ATTACH)
	{
		std::setlocale(LC_CTYPE, ".utf8");
		hooks = std::make_unique<Hooks>(moduleHandle);
		return TRUE;
	}

    return FALSE;
}
