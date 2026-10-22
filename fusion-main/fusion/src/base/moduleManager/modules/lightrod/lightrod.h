#pragma once
#include "config.h"
#include <Windows.h>

struct Lightrod {
    static void Init();
    static void Update();
    static void Shutdown();
    static lightrod::Settings GetSettings();
    static void Apply(lightrod::Settings value);
    static void Toggle();
    static bool Save();
    static std::string Notice();
    static const char* Status();
    static std::string KeyName(int key);
    static void BlockInput();
    static void SetMenuOpen(bool open);
    static void BeginCapture(int field);
    static void CancelCapture();
    static int Capturing();
    static bool Capture(int key);
    static bool SyntheticMessage(UINT msg);
    // The existing WndProc forwards this translated message directly to LWJGL.
    static bool TranslateInput(WPARAM w, LPARAM l, UINT& msg, WPARAM& key, LPARAM& param);
};
