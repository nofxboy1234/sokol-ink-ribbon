//------------------------------------------------------------------------------
//  tooltip.cc
//  A small dear-imgui helper for the runtime (main.c is C, so the C++ ImGui
//  call lives here): a black rectangle with white text.
//------------------------------------------------------------------------------
#include "tooltip.h"
#include "imgui.h"

extern "C" void game_tooltip(const char* text, float x, float y) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 tp(x, y);
    ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(ImVec2(tp.x - 5.0f, tp.y - 3.0f),
                      ImVec2(tp.x + ts.x + 5.0f, tp.y + ts.y + 3.0f),
                      IM_COL32(0, 0, 0, 235));
    dl->AddText(tp, IM_COL32(255, 255, 255, 255), text);
}
