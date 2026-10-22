#include "main.h"

namespace {
	DWORD WINAPI InitThread(LPVOID) { Main::Init(); return 0; }
}

void Main::Init()
{
	Base::Init();
}

void Main::Kill()
{
	Base::Kill();
	FreeLibraryAndExitThread(Main::HModule, 0);
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		Main::HModule = hModule;
		DisableThreadLibraryCalls(hModule);

		HANDLE hThread = CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);

		if (hThread) CloseHandle(hThread);
	}

	return TRUE;
}
