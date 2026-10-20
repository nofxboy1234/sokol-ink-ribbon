//------------------------------------------------------------------------------
//  imgui-glue/tooltip.cc
//  A small dear-imgui helper for the runtime (main.c is C, so the C++ ImGui
//  call lives here).
//------------------------------------------------------------------------------
#include "../src/tooltip.h"
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

extern "C" void game_label(const char* text, float x, float y) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 tp(x - ts.x * 0.5f, y - ts.y - 3.0f);
    ImVec2 a(tp.x - 5.0f, tp.y - 3.0f), b(tp.x + ts.x + 5.0f, tp.y + ts.y + 3.0f);
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.5f);
    dl->AddText(tp, IM_COL32(0, 0, 0, 255), text);
}

extern "C" bool game_ui_wants_mouse(void) {
    return ImGui::GetIO().WantCaptureMouse;
}
