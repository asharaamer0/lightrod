// Test-only hidden OpenGL surface. Renders the production menu without a JVM;
// this is visual QA, not a replacement for Lunar integration testing.
#include "../fusion/src/base/menu/menu.h"
#include "../fusion/src/base/moduleManager/modules/lightrod/lightrod.h"
#include "../fusion/ext/imgui/imgui_impl_opengl2.h"
#include "../fusion/ext/imgui/imgui_impl_win32.h"
#include <gl/GL.h>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "Lightrod.MenuTest"; wc.style = CS_OWNDC;
    if (!RegisterClassA(&wc)) return 1;
    HWND window = CreateWindowA(wc.lpszClassName, "Lightrod render test", WS_POPUP, 0, 0, 960, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 2;
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 24;
    if (!SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd)) return 3;
    HGLRC initial = wglCreateContext(dc); wglMakeCurrent(dc, initial);
    Menu::HandleWindow = window; Menu::HandleDeviceContext = dc; Menu::OriginalGLContext = initial;
    Menu::SetupImgui(); Menu::SetOpen(true);
    for (int frame = 0; frame < 40; ++frame) {
        ImGui_ImplOpenGL2_NewFrame(); ImGui_ImplWin32_NewFrame();
        ImGui::GetIO().DeltaTime = 1.f / 60.f;
        ImGui::NewFrame(); ImGui::PushFont(Menu::Font); Menu::RenderMenu(); ImGui::PopFont(); ImGui::Render();
        glViewport(0, 0, 960, 900); glClearColor(.065f, .065f, .07f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    }
    glFinish();
    std::vector<unsigned char> pixels(960 * 900 * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, 960, 900, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    for (size_t i = 0; i < pixels.size(); i += 3) std::swap(pixels[i], pixels[i + 2]);
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = 960; info.biHeight = 900; info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = static_cast<DWORD>(pixels.size());
    std::ofstream output(argc > 1 ? argv[1] : "menu-preview.bmp", std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file)); output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size()); output.close();
    if (!output) return 4;
    // Exercise the actual controller without initializing JVM-dependent hooks.
    Lightrod::BeginCapture(0); Lightrod::Capture(VK_XBUTTON1);
    if (Lightrod::GetSettings().trigger != VK_XBUTTON1 || Lightrod::Capturing() != -1) return 5;
    Lightrod::BeginCapture(1); Lightrod::Capture(VK_XBUTTON2);
    if (Lightrod::Capturing() != 1) return 6; // Slot keys reject mouse buttons.
    Lightrod::Capture(VK_ESCAPE);
    if (Lightrod::Capturing() != -1) return 7;
    Lightrod::Toggle(); if (std::string(Lightrod::Status()) != "OFF") return 8;
    Lightrod::Toggle(); if (std::string(Lightrod::Status()) != "READY") return 9;
    Lightrod::Apply(lightrod::Settings{});
    Lightrod::BeginCapture(3);
    Menu::SetOpen(false);
    if (Lightrod::Capturing() != -1) return 10;
    // Reversible entrance/exit and a smaller viewport must remain renderable.
    for (int frame = 0; frame < 45; ++frame) {
        ImGui_ImplOpenGL2_NewFrame(); ImGui_ImplWin32_NewFrame();
        ImGui::GetIO().DeltaTime = 1.f / 60.f;
        ImGui::NewFrame(); if (Menu::Visible()) Menu::RenderMenu(); ImGui::Render();
    }
    if (Menu::Visible()) return 11;
    SetWindowPos(window, nullptr, 0, 0, 640, 480, SWP_NOACTIVATE | SWP_NOZORDER);
    Menu::SetOpen(true);
    ImGui_ImplOpenGL2_NewFrame(); ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame(); ImGui::PushFont(Menu::Font); Menu::RenderMenu(); ImGui::PopFont(); ImGui::Render();
    if (!ImGui::GetDrawData()->TotalVtxCount) return 12;
    Menu::Kill(); wglMakeCurrent(nullptr, nullptr); wglDeleteContext(initial); ReleaseDC(window, dc); DestroyWindow(window);
    std::cout << "PASS production menu rendered/closed/resized; binding capture, Escape, and toggles work\n";
    return 0;
}
