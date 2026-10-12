//------------------------------------------------------------------------------
//  tooltip.cc
//  A small dear-imgui helper for the runtime (main.c is C, so the C++ ImGui
//  call lives here).
//------------------------------------------------------------------------------
#include "tooltip.h"
#include "imgui.h"

extern "C" void game_tooltip(const char* text, float x, float y, bool alert) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 tp(x, y);
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImU32 bg = alert ? IM_COL32(255, 0, 42, 235) : IM_COL32(0, 0, 0, 235);
    ImU32 fg = alert ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
    dl->AddRectFilled(ImVec2(tp.x - 5.0f, tp.y - 3.0f),
                      ImVec2(tp.x + ts.x + 5.0f, tp.y + ts.y + 3.0f),
                      bg);
    dl->AddText(tp, fg, text);
}
