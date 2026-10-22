#include "moduleManager.h"

#include "modules/visual/esp.h"
#include "modules/combat/aimAssist.h"
#include "modules/combat/reach.h"
#include "modules/clicker/leftAutoClicker.h"
#include "modules/clicker/rightAutoClicker.h"
#include "commonData.h"
#include "modules/lightrod/lightrod.h"

void ModuleManager::Init()
{
	Lightrod::Init();
	// This distribution exposes Lightrod only. Keep Fusion's unrelated modules
	// inactive, including its original default-on ESP.
	Esp::Enabled = false;
	AimAssist::Enabled = false;
	Reach::Enabled = false;
	LeftAutoClicker::Enabled = false;
	RightAutoClicker::Enabled = false;
}

void ModuleManager::UpdateModules()
{
	Lightrod::Update(); // Must also run outside a world so cancellation can release input.
	if (!(Esp::Enabled || AimAssist::Enabled || Reach::Enabled || LeftAutoClicker::Enabled || RightAutoClicker::Enabled)) return;
	if (!CommonData::SanityCheck()) return;

	CommonData::UpdateData();
	Esp::Update();

	AimAssist::Update();
	Reach::Update();

	LeftAutoClicker::Update();
	RightAutoClicker::Update();
}
