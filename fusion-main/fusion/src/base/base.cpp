#include "base.h"

#include "../main.h"
#include "java/java.h"
#include "util/logger.h"
#include "menu/menu.h"
#include "moduleManager/moduleManager.h"
#include "sdk/sdk.h"
#include "util/window/borderless.h"
#include "patcher/patcher.h"
#include "moduleManager/modules/lightrod/lightrod.h"

#include "../../ext/minhook/minhook.h"

#include <thread>
#include <unordered_map>
#include <mmsystem.h>

bool IsKeyReleased(int key)
{
	static std::unordered_map<int, bool> keyStates;

	bool currentState = (GetAsyncKeyState(key) & 0x8000) == 0;
	bool prevState = keyStates[key];
	keyStates[key] = currentState;

	return prevState && !currentState;
}

void Base::Init()
{
	MH_Initialize();

	Java::Init();
	if (!Java::Initialized) {
		OutputDebugStringA("[Lightrod] Lunar 1.8.9 JVM/mappings unavailable; initialization cancelled.\n");
		Java::Kill(); MH_Uninitialize();
		FreeLibraryAndExitThread(Main::HModule, 1);
	}
	try { SDK::Init(); }
	catch (const std::exception& error) {
		OutputDebugStringA(error.what());
		if (Java::Env->ExceptionCheck()) Java::Env->ExceptionClear();
		Java::Kill(); MH_Uninitialize();
		FreeLibraryAndExitThread(Main::HModule, 1);
	}
	if (Java::Env->ExceptionCheck()) {
		Java::Env->ExceptionClear();
		OutputDebugStringA("[Lightrod] Fusion 1.8.9 mappings are incompatible with this client.\n");
		Java::Kill(); MH_Uninitialize();
		FreeLibraryAndExitThread(Main::HModule, 1);
	}
	ModuleManager::Init();
	Base::Running = true;
	Menu::Init();

	if (!Menu::RenderHookReady) Base::Running = false;
	const bool preciseTimer = timeBeginPeriod(1) == TIMERR_NOERROR;

	while (Base::Running)
	{
		if (Java::Env->PushLocalFrame(512) == JNI_OK) {
			try { ModuleManager::UpdateModules(); }
			catch (const std::exception& error) {
				Lightrod::BlockInput();
				OutputDebugStringA(error.what());
			}
			if (Java::Env->ExceptionCheck()) { Java::Env->ExceptionClear(); Lightrod::BlockInput(); }
			Java::Env->PopLocalFrame(nullptr);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	if (preciseTimer) timeEndPeriod(1);
	Main::Kill();
}	

void Base::Kill()
{
	Lightrod::Shutdown();
	if (Borderless::Enabled)
		Borderless::Restore(Menu::HandleWindow);

	Menu::Kill();
	StrayCache::DeleteRefs();
	Java::Kill();
	Logger::Kill();

	MH_Uninitialize();
}
