#pragma once

namespace Desert::Editor::Core
{
    // Shared state between the docked ModelingPanel (the UE5-style tool palette + properties) and the
    // viewport tool that actually edits geometry (CubeGridTool). The panel selects the active tool + edits
    // its properties + posts Accept/Cancel/Clear requests; the tool reads them and reports back its stats.
    // A tiny global singleton mirroring ViewportMode.
    class ModelingState
    {
    public:
        enum class Tool
        {
            None = 0,
            CubeGrid,
            PolyEdit
        };
        enum class Output // where a committed blockout goes
        {
            StaticMesh = 0,
            DynamicMesh
        };

        static ModelingState& Get()
        {
            static ModelingState s;
            return s;
        }

        // Panel -> tool
        Tool   ActiveTool = Tool::None;
        Output OutputType = Output::DynamicMesh;
        float  CellSize   = 100.0f; // grid step
        bool   ReqAccept  = false;  // one-shot: commit the blockout, start a fresh one
        bool   ReqCancel  = false;  // one-shot: delete the in-progress blockout
        bool   ReqClear   = false;  // one-shot: clear the cells (keep editing)

        // Tool -> panel (read-only stats for the properties panel)
        int Cubes = 0;
    };
} // namespace Desert::Editor::Core
