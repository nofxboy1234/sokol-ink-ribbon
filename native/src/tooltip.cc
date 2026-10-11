//------------------------------------------------------------------------------
//  tooltip.cc
//  A small dear-imgui helper for the runtime (main.c is C, so the C++ ImGui
//  call lives here): a black rectangle with white text.
//------------------------------------------------------------------------------
#include "tooltip.h"
#include "imgui.h"
#include <cstdio>

extern "C" void game_tooltip(const char* text, float x, float y) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 tp(x, y);
    ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(ImVec2(tp.x - 5.0f, tp.y - 3.0f),
                      ImVec2(tp.x + ts.x + 5.0f, tp.y + ts.y + 3.0f),
                      IM_COL32(0, 0, 0, 235));
    dl->AddText(tp, IM_COL32(255, 255, 255, 255), text);
}

// Save/load menu shaped like the ref/map.kra "Load menu": a slot table with
// the saved time (or NO DATA) and Save/Load buttons.
extern "C" int game_save_menu(bool* open, const double* times, const unsigned char* used, int nslots) {
    if (!*open) {
        return 0;
    }
    int action = 0;
    ImGui::SetNextWindowSize(ImVec2(330.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Save / Load", open)) {
        if (ImGui::BeginTable("slots", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("slot");
            ImGui::TableSetupColumn("time");
            ImGui::TableSetupColumn("actions");
            for (int i = 0; i < nslots; i++) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Slot %d", i + 1);
                ImGui::TableSetColumnIndex(1);
                if (used[i]) {
                    int ms = (int)times[i];
                    ImGui::Text("%02d:%02d.%02d", ms / 60000, (ms / 1000) % 60, (ms % 1000) / 10);
                } else {
                    ImGui::TextUnformatted("NO DATA");
                }
                ImGui::TableSetColumnIndex(2);
                char lbl[32];
                snprintf(lbl, sizeof(lbl), "Save##%d", i);
                if (ImGui::SmallButton(lbl)) {
                    action = i + 1;
                }
                ImGui::SameLine();
                snprintf(lbl, sizeof(lbl), "Load##%d", i);
                if (ImGui::SmallButton(lbl)) {
                    action = 100 + i;
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Close")) {
            *open = false;
            action = -1;
        }
    }
    ImGui::End();
    return action;
}
