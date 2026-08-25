#pragma once

#include "../IPanel.hpp"

#include <Engine/Assets/CloudLayout.hpp>
#include <Engine/Assets/CloudProceduralVolume.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Desert::Assets
{
    class AssetManager;
}
namespace Desert::Graphic
{
    class Image2D;
}

namespace Desert::Editor
{
    /**
     * @brief The artist's tool for a PAINTED SKY: point at a picture, say which of its channels feeds
     *        which slot, look at the sky it makes, bake it to a `.dclayout`.
     *
     * WHY IT EXISTS. Phase PT shipped the format, the engine path, the Details slot with its picker and
     * its drag-and-drop target, and `Tools/CloudLayoutBaker`. That closes half the loop: an artist could
     * ATTACH a finished painting but could only MAKE one from a terminal. The owner asked for the editor.
     *
     * IT IS BUILT AFTER THE CLOUD NOISE VOLUME PANEL NEXT DOOR, deliberately and down to the section
     * order — source, preview, save — because that panel already does work of this shape and a second
     * layout for the same job is a second thing to learn.
     *
     * THE BAKE CALLS THE SAME FUNCTION THE COMMAND-LINE TOOL CALLS. `Assets::MakeCloudLayoutFromImage`
     * then `Assets::CloudLayoutAsset::Save`, which is `Tools/CloudLayoutBaker/Source/main.cpp` with a file
     * dialog in front of it. A panel whose output differed from the tool's would be the second path §1.3
     * and §4.2 of the contract forbid, and the difference would show up as a sky rather than as an error.
     *
     * THE PREVIEW SHOWS THE SKY, NOT THE TEXTURE, and that is the panel's one real design decision.
     * Showing the painting is honest and nearly useless: an artist draws a sky, and §PT's own proof that
     * the feature works is a TOP-DOWN frame rather than a texture viewer. So the right-hand pane is a
     * top-down map of the coverage the painting produces, built by `Assets::BuildCloudLayoutPreview` —
     * which calls the very function the bake calls — sampled on the PLACEMENT CELL, because that is the
     * resolution the sky can express. The painting is shown beside it at its own resolution, and the
     * difference between the two panes is the whole of what an artist needs to learn.
     *
     * AND IT SHOWS THE TWO THINGS §PT MEASURED AND ONLY THE PROTOCOL KNEW:
     *
     *   1. PATTERN STRENGTH AND MASK STRENGTH ARE NOT INDEPENDENT. With the mask at full strength a pair
     *      of frames across the whole pattern slider came back byte for byte identical, because the mask
     *      alone drove the figure's inside over 1 and its outside under 0 and the clamp then ate the
     *      pattern's entire contribution. The panel counts the cells the two ends of the pattern slider
     *      disagree about and says so when the answer is none.
     *   2. LEGIBILITY IS BOUNDED BY THE STROKE, AND THE VALIDATOR CHECKS THE TEXEL.
     *      `ValidateCloudProceduralLayout` compares one layout texel against the cell, which is the right
     *      bound for "can the painting tell two cells apart" and the wrong one for "does this still read
     *      as a letter". The panel measures the painting's own strokes
     *      (`Assets::MeasureCloudLayoutStrokes`) and reports how much of the drawing is finer than a
     *      cloud cell. Reported and never refused, because a stroke width is a fact about a picture and a
     *      threshold on it would be an opinion about somebody's art.
     *
     * NO BACKGROUND WORK, unlike the noise volume panel: building a 512-square layout from an image is
     * one pass over a million texels and the map is a few thousand evaluations of a closed-form
     * expression, so both are recomputed when something changes and never per frame.
     *
     * THE PREVIEW'S LAYER SETTINGS ARE THE PANEL'S OWN AND ARE NOT WRITTEN TO THE FILE. A `.dclayout`
     * carries pixels; where a painting sits in the world and how hard it pushes are the cloud LAYER's
     * fields, on the component, and duplicating them into the file would be two values obliged to agree
     * (§4.2). They are here so the artist can see what their picture will do at the settings they intend
     * to use, and they default to the shipped scene's.
     */
    class CloudLayoutPanel final : public IPanel
    {
    public:
        explicit CloudLayoutPanel( Assets::AssetManager* assets );

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 700.0f, 820.0f );
        }

        void OnUIRender() override;

    private:
        void DrawSourceSection();
        void DrawChannelSection();
        void DrawLayerSection();
        void DrawPreviewSection();
        void DrawVerdictSection();
        void DrawSaveSection();

        /// Reads an image off disk into m_Source. Named so the file dialog and the drag-and-drop target are
        /// one operation and cannot decode a picture two different ways.
        void LoadImage( const std::filesystem::path& path );

        /// Rebuilds m_Layout from m_Source through Assets::MakeCloudLayoutFromImage — the tool's function,
        /// not a second reading of what a picture means.
        void RebuildLayout();

        /// Rebuilds m_Preview, the stroke statistics and both device images. Called when the layout, the
        /// layer settings or the previewed slot change — never per frame, because it allocates images.
        void RefreshPreview();

        /// The parameters a layer with these panel settings would hand the bake. One place, so the map and
        /// the numbers beside it cannot describe different skies.
        Assets::CloudProceduralFieldParams BuildParams() const;

        Assets::AssetManager* m_Assets = nullptr;

        // ---- source -----------------------------------------------------------------------------------

        std::vector<unsigned char> m_SourcePixels; // RGBA8, x fastest, exactly as stbi_load returns it
        uint32_t                   m_SourceWidth  = 0u;
        uint32_t                   m_SourceHeight = 0u;
        std::string                m_SourceName;   // the picture's file name, or the .dclayout's

        /// Which SOURCE channel feeds each species slot. THE CONVENTION IS A CONTROL AND NOT AN ASSUMPTION:
        /// a painting is usually greyscale, and without this an artist wanting one drawing on slot 2 would
        /// have to author an RGBA image to say so. Defaults to straight RGBA, which is the tool's default.
        uint32_t m_ChannelForSlot[Assets::kCloudLayoutChannels] = { 0u, 1u, 2u, 3u };

        /// Take the source's alpha as the add/remove mask. OFF by default and not "alpha is always the
        /// mask", because an opaque PNG has alpha 255 everywhere, which under the signed convention is a
        /// mask that adds cloud to the whole sky — a silent, uniform, wrong answer.
        bool m_TakeMask = false;

        // ---- the layout being authored ----------------------------------------------------------------

        Assets::CloudLayoutData m_Layout;
        bool                    m_HasLayout = false;

        /// True when m_Layout came from a `.dclayout` rather than from a picture. The channel controls
        /// describe an image-to-slot mapping and there is no image, so they are disabled and say why —
        /// rather than sitting there implying they would do something.
        bool m_LayoutFromFile = false;

        // ---- the layer the preview assumes ------------------------------------------------------------
        // Defaults are the shipped scene's: Region Size 48 km, a 3 km placement cell (Weather Tile Size
        // 12 km, four cells to a tile), Coverage 0.45, Weather Patch Strength 0.60, Seed 1.

        float    m_RegionSizeKm = 48.0f;
        float    m_CellKm       = 3.0f;
        float    m_Coverage     = 0.45f;
        float    m_PatchStrength = 0.60f;
        int      m_Seed         = 1;
        int      m_Repeats      = 1;
        int      m_QuarterTurns = 0;
        float    m_OffsetKm[2]  = { 0.0f, 0.0f };
        float    m_PatternStrength = 1.0f;
        float    m_MaskStrength    = 1.0f;

        // ---- preview ----------------------------------------------------------------------------------

        enum class PaintingView : int
        {
            Slot0 = 0,
            Slot1,
            Slot2,
            Slot3,
            Mask
        };

        int          m_PreviewSlot  = 0;
        PaintingView m_PaintingView = PaintingView::Slot0;
        int          m_SpanRegions  = 1;   // how many region periods the sky map covers, so tiling is visible
        int          m_PreviewSide  = 256; // pixels the two panes are drawn at

        Assets::CloudLayoutPreview     m_Preview;
        bool                           m_HasPreview = false;
        Assets::CloudLayoutStrokeStats m_Strokes;

        std::shared_ptr<Graphic::Image2D> m_PaintingImage;
        std::shared_ptr<Graphic::Image2D> m_SkyImage;

        bool m_PreviewDirty = true;

        // ---- status -----------------------------------------------------------------------------------

        std::string m_Status;
        bool        m_StatusIsError = false;
    };
} // namespace Desert::Editor
