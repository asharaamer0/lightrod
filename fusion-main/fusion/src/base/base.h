#pragma once
#include <atomic>

struct Base
{
	static void Init();
	static void Kill();

	static void CheatLoop();
	static void RenderLoop();

	static inline std::atomic<bool> Running{false};
};

