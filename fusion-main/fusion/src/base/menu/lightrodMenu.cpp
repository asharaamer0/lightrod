#include "menu.h"
#include "../moduleManager/modules/lightrod/lightrod.h"
#include "../base.h"
#include <algorithm>
#include <cmath>

namespace {
float visibility = 0.0f;
bool button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x == 0) size.x = textSize.x + ImGui::GetStyle().FramePadding.x * 2;
    if (size.y == 0) size.y = textSize.y + ImGui::GetStyle().FramePadding.y * 2;
    const bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    float* blend = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(label), 0);
    *blend += ((hovered ? 1.f : 0.f) - *blend) * (1.f - std::exp(-ImGui::GetIO().DeltaTime * 24));
    const ImVec4 normal = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    const ImVec4 hover = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    const ImVec4 color(normal.x + (hover.x - normal.x) * *blend, normal.y + (hover.y - normal.y) * *blend,
        normal.z + (hover.z - normal.z) * *blend, 1);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    const bool result = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return result;
}
const char* clickName(lightrod::Click click) {
    return click == lightrod::Click::None ? "None" : click == lightrod::Click::Left ? "Left" : "Right";
}
void section(const char* label) {
    ImGui::Spacing(); ImGui::TextDisabled("%s", label); ImGui::Separator(); ImGui::Spacing();
}
void binding(const char* label, int value, int field, float width) {
    ImGui::PushID(field);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
    ImGui::SameLine(width - 182);
    const bool capturing = Lightrod::Capturing() == field;
    auto text = capturing ? "Press input..." : Lightrod::KeyName(value);
    if (capturing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.35f, .35f, .36f, 1));
    if (button(text.c_str(), ImVec2(158, 28))) Lightrod::BeginCapture(field);
    if (capturing) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(field == 0 || field == 3 ? "Keyboard or mouse. Escape cancels." : "Keyboard input. Escape cancels.");
    ImGui::PopID();
}
bool timing(const char* label, const char* hint, int& value, int minimum) {
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
    ImGui::SetNextItemWidth(-1);
    const std::string id = std::string("##") + label;
    const bool changed = ImGui::InputInt(id.c_str(), &value, 1, 10);
    value = std::clamp(value, minimum, 2000);
    return changed;
}
}
void Menu::SetOpen(bool open) {
    if (Open.exchange(open) == open) return;
    Lightrod::SetMenuOpen(open);
}
bool Menu::Visible() { return Open || visibility > .001f; }

void Menu::RenderMenu() {
    ImGuiIO& io = ImGui::GetIO();
    const float target = Open ? 1.f : 0.f;
    visibility += (target - visibility) * (1.f - std::exp(-io.DeltaTime * (Open ? 22.f : 30.f)));
    if (std::abs(target - visibility) < .001f) visibility = target;
    const float width = (std::min)(480.f, (std::max)(280.f, io.DisplaySize.x - 24.f));
    const float height = (std::min)(780.f, (std::max)(220.f, io.DisplaySize.y - 24.f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f + 8 * (1 - visibility)), ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, visibility);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (!Open) flags |= ImGuiWindowFlags_NoInputs;
    ImGui::Begin("Lightrod", nullptr, flags);
    auto s = Lightrod::GetSettings();
    bool changed = false;
    ImGui::PushFont(Menu::FontBold); ImGui::TextUnformatted("Lightrod"); ImGui::PopFont();
    ImGui::SameLine(width - 68);
    if (button("X", ImVec2(24, 24))) SetOpen(false);
    ImGui::TextDisabled("ROD MACRO   /   Sword & rod sequence");
    ImGui::Spacing();
    const char* status = Lightrod::Status();
    const float pulse = .55f + .25f * static_cast<float>(std::sin(ImGui::GetTime() * 3));
    ImGui::TextColored(ImVec4(pulse, pulse, pulse, 1), "o"); ImGui::SameLine();
    ImGui::TextUnformatted(status); ImGui::SameLine();
    const std::string hint = s.enabled ? "Hold " + Lightrod::KeyName(s.trigger) + " to run" : "Press " + Lightrod::KeyName(s.toggle) + " to enable";
    ImGui::TextDisabled("  %s", hint.c_str());

    section("01  KEYBINDS");
    binding("Trigger / hold", s.trigger, 0, width);
    binding("Sword / slot 1", s.sword, 1, width);
    binding("Rod / slot 2", s.rod, 2, width);
    binding("Enable / disable", s.toggle, 3, width);
    if (Lightrod::Capturing() >= 0) ImGui::TextDisabled("Waiting for input. Escape cancels.");

    section("02  CLICK ACTIONS");
    const auto clickRow = [&](const char* label, lightrod::Click& value) {
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label); ImGui::SameLine(width - 182);
        if (button(clickName(value), ImVec2(158, 28))) {
            value = static_cast<lightrod::Click>((static_cast<int>(value) + 1) % 3); changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to cycle: None / Left / Right");
        ImGui::PopID();
    };
    clickRow("Sword / phase A", s.swordClick); clickRow("Rod / phase B", s.rodClick);
    changed |= ImGui::Checkbox("Return to sword after rod", &s.returnToSword);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the sword without another click after the rod action.\nAlso restores it on trigger release during gameplay.\nDisable for the original next-cycle return.");

    section("03  TIMING / MILLISECONDS");
    ImGui::Columns(2, "timing", false);
    changed |= timing("Key hold / ms", "Base press duration. Effective hold is limited to fit both phases.", s.hold, 1);
    changed |= timing("Delay 1 / ms", "Target time from cycle start to rod input. Lower is faster.", s.delay1, 10);
    ImGui::NextColumn();
    changed |= timing("Click sync / ms", "Gap between slot input and mouse press, in either phase.", s.sync, 0);
    changed |= timing("Delay 2 / ms", "Extends the cycle after Delay 1. Lower is faster.", s.delay2, 10);
    ImGui::Columns(1);
    ImGui::TextDisabled("Cycle: %d ms   /   Effective hold: %d ms", s.delay1 + s.delay2, s.effectiveHold());
    ImGui::TextDisabled("Lower delays repeat faster; short inputs may be dropped.");
    if (s.sync + s.effectiveHold() >= (std::min)(s.delay1, s.delay2))
        ImGui::TextWrapped("Click sync exceeds the phase budget. The sequence will run slower than the target.");
    const auto warning = lightrod::bindingWarning(s);
    if (!warning.empty()) ImGui::TextWrapped("%s", warning.c_str());
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    if (button(s.enabled ? "Disable macro" : "Enable macro", ImVec2(170, 32))) { s.enabled = !s.enabled; changed = true; }
    ImGui::SameLine(); const bool save = button("Save", ImVec2(88, 32));
    ImGui::SameLine();
    if (button("Reset", ImVec2(88, 32))) { s = lightrod::Settings{}; changed = true; Lightrod::CancelCapture(); }
    if (changed) { Lightrod::Apply(s); Lightrod::Save(); }
    else if (save) Lightrod::Save();
    const auto notice = Lightrod::Notice();
    ImGui::TextDisabled("%s", notice.empty() ? "Auto-saved on change. Insert / Escape to close." : notice.c_str());
    if (ImGui::SmallButton("Unload Lightrod")) { SetOpen(false); Base::Running = false; }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
void Menu::RenderStatus() {
    const auto s = Lightrod::GetSettings();
    const std::string label = std::string("Lightrod  /  ") + Lightrod::Status() + "   |   " +
        (s.enabled ? "Hold " + Lightrod::KeyName(s.trigger) : "Enable " + Lightrod::KeyName(s.toggle)) + "   |   Insert";
    ImGui::GetForegroundDrawList()->AddText(ImVec2(16, 16), IM_COL32(222, 221, 216, 220), label.c_str());
}
