#include "../menu.h"

#include <gl/GL.h>
#include <mutex>
#include <Shlobj.h>

#include "../../../../ext/minhook/minhook.h"
#include "../../../../ext/imgui/imgui.h"
#include "../../../../ext/imgui/imgui_impl_win32.h"
#include "../../../../ext/imgui/imgui_impl_opengl2.h"
#include "../../util/logger.h"
#include "../../util/trimmer.h"
#include "../../../../ext/fonts/jetbrainsmono.h"

#include "../../base.h"
#include "../../moduleManager/modules/lightrod/lightrod.h"
#include <atomic>

std::once_flag setupFlag;
std::atomic_flag clipCursor = ATOMIC_FLAG_INIT;
RECT originalClip;

typedef bool(__stdcall* template_wglSwapBuffers) (HDC hdc);
template_wglSwapBuffers original_wglSwapBuffers;
bool __stdcall hook_wglSwapBuffers(_In_ HDC hdc)
{
	Menu::CallbackScope callback;
	// handling fullscreen context switching before we set the new hwnd, so we can compare them
	
	// if the cached hwnd isnt equal to the current one
	// info: window handles change when you enter/exit fullscreen
	if (Menu::HandleWindow != WindowFromDC(hdc) && Menu::Initialized)
	{
		Lightrod::BlockInput();
		Menu::SetOpen(false);
		Menu::Unhook_wndProc();
		clipCursor.clear();
		originalClip = {};
		// set handlewindow so that the wndproc can attach to new one before passing
		Menu::HandleWindow = WindowFromDC(hdc);

		// opengl context for font issue
		Menu::OriginalGLContext = wglGetCurrentContext();
		if (!wglMakeCurrent(hdc, Menu::MenuGLContext)) {
			OutputDebugStringA("[Lightrod] OpenGL context could not be rebound after a window change.\n");
			Base::Running = false;
			return original_wglSwapBuffers(hdc);
		}

		// uninitialize imgui opengl and win32 implementation
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplWin32_Shutdown();
		wglMakeCurrent(hdc, Menu::OriginalGLContext);
		wglDeleteContext(Menu::MenuGLContext);
		Menu::MenuGLContext = wglCreateContext(hdc);
		if (!Menu::MenuGLContext || !wglMakeCurrent(hdc, Menu::MenuGLContext)) {
			if (Menu::MenuGLContext) wglDeleteContext(Menu::MenuGLContext);
			Menu::MenuGLContext = nullptr;
			ImGui::DestroyContext();
			Menu::Initialized = false;
			wglMakeCurrent(hdc, Menu::OriginalGLContext);
			OutputDebugStringA("[Lightrod] Failed to recreate the menu context.\n");
			Base::Running = false;
			return original_wglSwapBuffers(hdc);
		}
		
		// reinitialize it
		ImGui_ImplWin32_Init(Menu::HandleWindow);
		ImGui_ImplOpenGL2_Init();

		// set wndproc
		Menu::Hook_wndProc();

		// revert to old ctx
		wglMakeCurrent(hdc, Menu::OriginalGLContext);

		// end detour
		return original_wglSwapBuffers(hdc);
	}

	Menu::HandleDeviceContext = hdc;
	Menu::HandleWindow = WindowFromDC(hdc);
	Menu::OriginalGLContext = wglGetCurrentContext();

	std::call_once(setupFlag, [&] {
		Menu::Hook_wndProc();
		Menu::SetupImgui();
	});
	if (!Menu::Initialized) return original_wglSwapBuffers(hdc);

	if (!wglMakeCurrent(Menu::HandleDeviceContext, Menu::MenuGLContext)) {
		Base::Running = false;
		return original_wglSwapBuffers(hdc);
	}

	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::PushFont(Menu::Font);

	if (Menu::Visible())
	{
		if (!clipCursor.test_and_set()) GetClipCursor(&originalClip);

		ClipCursor(NULL);
		Menu::RenderMenu();
	}
	else
	{
		// checking if originalClip is valid
		if (clipCursor.test())
		{
			clipCursor.clear();
			if (GetForegroundWindow() == Menu::HandleWindow && originalClip.right > originalClip.left && originalClip.bottom > originalClip.top)
				ClipCursor(&originalClip);
			else ClipCursor(nullptr);
		}
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin("Overlay", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoBackground);

	Base::RenderLoop();

	ImGui::PopFont();
	ImGui::End();

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

	wglMakeCurrent(Menu::HandleDeviceContext, Menu::OriginalGLContext);
	return original_wglSwapBuffers(hdc);
}

void Menu::Hook_wglSwapBuffers()
{
	HMODULE glModule = GetModuleHandleA("opengl32.dll");
	LPVOID wglSwapBuffers = glModule ? (LPVOID)GetProcAddress(glModule, "wglSwapBuffers") : nullptr;
	Menu::RenderHookReady = wglSwapBuffers &&
		MH_CreateHook(wglSwapBuffers, (LPVOID)hook_wglSwapBuffers, (LPVOID*)&original_wglSwapBuffers) == MH_OK &&
		MH_EnableHook(wglSwapBuffers) == MH_OK;
	if (!Menu::RenderHookReady) OutputDebugStringA("[Lightrod] Could not attach the existing Fusion OpenGL render hook.\n");
}

void Menu::Unhook_wglSwapBuffers()
{	
	MH_DisableHook(MH_ALL_HOOKS);
	MH_RemoveHook(MH_ALL_HOOKS);
}

void Menu::SetupImgui()
{
	Menu::MenuGLContext = wglCreateContext(Menu::HandleDeviceContext);
	if (!Menu::MenuGLContext || !wglMakeCurrent(Menu::HandleDeviceContext, Menu::MenuGLContext)) {
		if (Menu::MenuGLContext) wglDeleteContext(Menu::MenuGLContext);
		Menu::MenuGLContext = nullptr;
		OutputDebugStringA("[Lightrod] Could not create the menu OpenGL context.\n");
		Base::Running = false;
		return;
	}

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	GLint m_viewport[4];
	glGetIntegerv(GL_VIEWPORT, m_viewport);

	glOrtho(0, m_viewport[2], m_viewport[3], 0, 1, -1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glClearColor(0, 0, 0, 0);

	Menu::CurrentImGuiContext = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault();

	//Menu::Font = io.Fonts->AddFontFromMemoryTTF(jetbrainsmono, sizeof(jetbrainsmono), 16);
	char windowsPath[MAX_PATH]{};
	GetWindowsDirectoryA(windowsPath, MAX_PATH);
	const std::string fontDir = std::string(windowsPath) + "\\Fonts\\";
	const auto loadFont = [&](const char* name, float size) -> ImFont* {
		const auto path = fontDir + name;
		return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES ? io.Fonts->AddFontFromFileTTF(path.c_str(), size) : nullptr;
	};
	Menu::Font = loadFont("Helvetica.ttf", 16);
	if (!Menu::Font) Menu::Font = loadFont("arial.ttf", 16);
	if (!Menu::Font) Menu::Font = io.Fonts->Fonts[0];
	Menu::FontBold = loadFont("arialbd.ttf", 26);
	if (!Menu::FontBold) Menu::FontBold = Menu::Font;
	io.IniFilename = nullptr;

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(1.f, 1.f, 1.f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.045f, 0.045f, 0.90);
	colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.045f, 0.045f, 0.045f, 0.90);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.045f, 0.045f, 0.045f, 0.90);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.045f, 0.045f, 0.045f, 0.90);
	colors[ImGuiCol_TitleBg] = ImVec4(0.065f, 0.065f, 0.065f, 0.90);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.065f, 0.065f, 0.065f, 0.90);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.065f, 0.065f, 0.065f, 0.90);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.00);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.56f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.4, 0.7f, 0.7f, 1.0f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5, 0.89f, 0.89f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.19f, 0.19f, 0.19f, 1.f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.3f, 0.3f, 0.3f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.33f, 0.35f, 0.36f, 0.53f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.f, 0.44f, 0.44f, 0.67f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.47f, 0.47f, 0.67f);
	colors[ImGuiCol_Separator] = ImVec4(0.32f, 0.32f, 0.32f, 0.3);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.60f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
	colors[ImGuiCol_Tab] = ImVec4(0.07f, 0.07f, 0.07f, 0.51f);
	colors[ImGuiCol_TabHovered] = ImVec4(0, 0.23f, 0.23f, 0.67f);
	colors[ImGuiCol_TabActive] = ImVec4(0.19f, 0.19f, 0.19f, 0.57f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.05f, 0.05f, 0.05f, 0.90f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.13f, 0.13f, 0.74f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
	colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 10.0f;
	style.WindowBorderSize = 0;
	style.WindowPadding = ImVec2(0,0);
	// Lightrod's neutral palette overrides the inherited Fusion theme.
	colors[ImGuiCol_Text] = ImVec4(.90f, .89f, .86f, 1);
	colors[ImGuiCol_TextDisabled] = ImVec4(.59f, .59f, .57f, 1);
	colors[ImGuiCol_WindowBg] = ImVec4(.105f, .105f, .11f, .98f);
	colors[ImGuiCol_Border] = ImVec4(.25f, .25f, .26f, 1);
	colors[ImGuiCol_FrameBg] = ImVec4(.17f, .17f, .18f, 1);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(.22f, .22f, .23f, 1);
	colors[ImGuiCol_FrameBgActive] = ImVec4(.27f, .27f, .28f, 1);
	colors[ImGuiCol_Button] = ImVec4(.19f, .19f, .20f, 1);
	colors[ImGuiCol_ButtonHovered] = ImVec4(.27f, .27f, .28f, 1);
	colors[ImGuiCol_ButtonActive] = ImVec4(.34f, .34f, .35f, 1);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(.45f, .45f, .46f, .5f);
	colors[ImGuiCol_CheckMark] = ImVec4(.85f, .84f, .81f, 1);
	colors[ImGuiCol_NavHighlight] = ImVec4(.7f, .7f, .68f, 1);
	style.WindowRounding = 7;
	style.WindowBorderSize = 1;
	style.FrameRounding = 3;
	style.FramePadding = ImVec2(9, 6);
	style.ItemSpacing = ImVec2(10, 7);
	style.ScrollbarSize = 8;

	ImGui_ImplWin32_Init(Menu::HandleWindow);
	ImGui_ImplOpenGL2_Init();


	Menu::Initialized = true;
}
