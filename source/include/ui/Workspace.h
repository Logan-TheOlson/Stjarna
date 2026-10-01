#pragma once
#include <imgui.h>

// The docked-panel shell around the sim view: a slim top bar plus a dockspace whose free central
// area is where the simulation is drawn. The two side panels ("###LeftPanel" / "###RightPanel" —
// stable ids so each can change its visible title with app state) are docked left and right by
// default; the user can rearrange them and the layout persists in imgui.ini.
namespace Workspace {
    inline constexpr const char* LeftPanelId  = "###LeftPanel";
    inline constexpr const char* RightPanelId = "###RightPanel";

    struct Frame {
        // The free central area, in ImGui coordinates (points) — where the sim is shown.
        ImVec2 centerMin, centerMax;
    };

    // Rebuilds the default left/right layout on the next Begin().
    void RequestLayoutReset();

    // The top bar is a window the caller fills: `if (BeginTopBar()) { ... } EndTopBar();`
    // (EndTopBar is always required, matching ImGui::End). Call both before DockSpace().
    bool BeginTopBar();
    void EndTopBar();

    // Submits the dockspace (building the default layout if there's none yet) and returns the
    // free central area for this frame. Call once per frame, before submitting the panel windows.
    Frame DockSpace();
}
