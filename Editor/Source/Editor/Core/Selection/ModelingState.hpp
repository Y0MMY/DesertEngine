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

        // Smallest CubeGrid block, in world units (= 1 cm). Both the panel and the tool clamp to it.
        static constexpr float MinCellSize = 1.0f;

        // Panel -> tool
        Tool   ActiveTool = Tool::None;
        Output OutputType = Output::DynamicMesh;
        // CubeGrid block size (Resize Grid) in world units — one unit is one centimetre, so the default
        // 100 is a one-metre block, the same default as UE.
        float CellSize = 100.0f;
        // (legacy paint-brush fields, retained for compatibility; unused by the marquee CubeGrid)
        float  BrushW        = 1.0f;
        float  BrushD        = 1.0f;
        float  Height        = 1.0f;
        int    BlocksPerStep = 1;      // cells extruded/removed per Push/Pull (UE "Blocks Per Step")
        bool   ReqAccept  = false;  // one-shot: commit the blockout, start a fresh one
        bool   ReqCancel  = false;  // one-shot: delete the in-progress blockout
        bool   ReqClear   = false;  // one-shot: clear the cells (keep editing)

        // Tool -> panel (read-only stats for the properties panel)
        int Cubes = 0;
    };
} // namespace Desert::Editor::Core
