#pragma once
#include "engine.h"
#include <filesystem>
#include <string>

namespace lightrod {
bool isMouse(int key);
bool validBinding(int key, bool mouseAllowed);
Settings validate(Settings value);
std::string bindingWarning(const Settings& value);
Settings loadConfig(const std::filesystem::path& path);
bool saveConfig(const std::filesystem::path& path, const Settings& value, std::string& error);
}
