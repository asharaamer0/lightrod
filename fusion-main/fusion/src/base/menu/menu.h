#pragma once

#include <string>
#include <Windows.h>
#include <vector>
#include <atomic>

#include "../../../ext/imgui/imgui.h"

struct Menu
{
	static void Init();
	static void Kill();

	static inline std::string Title;
	static inline std::atomic<bool> Open{false};
	static inline int Keybind;
	static inline ImFont* Font;
	static inline ImFont* FontBold;
	static inline std::atomic<bool> Initialized{false};
	static inline bool RenderHookReady = false;
	static inline std::atomic<bool> InputHookReady{false};

	static void SetupImgui();
	static void RenderMenu();
	static void RenderLegacyMenu();
	static void SetOpen(bool open);
	static bool Visible();
	static void RenderStatus();

	static void ToggleButton(const char* format, bool* value);
	static bool TabButton(const char* format, ImVec4 color);
	static void DoSliderStuff(int id, const char* text, float* bruh, float min, float max);
	static void DoToggleButtonStuff(int id, const char* text, bool* bruh);
	static void GlitchText(const char* text, ImVec2 pos);

	static inline std::atomic<HWND> HandleWindow{nullptr};
	static inline std::atomic<unsigned> ActiveCallbacks{0};
	struct CallbackScope {
		CallbackScope() { ++ActiveCallbacks; }
		~CallbackScope() { --ActiveCallbacks; }
	};
	static inline HDC HandleDeviceContext;

	static inline HGLRC OriginalGLContext;
	static inline HGLRC MenuGLContext;

	static inline ImGuiContext* CurrentImGuiContext;

	static void PlaceHooks();
	static void RemoveHooks();

	static void Hook_wglSwapBuffers();
	static void Hook_wndProc();

	static void Unhook_wglSwapBuffers();
	static void Unhook_wndProc();
};

