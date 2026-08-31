#pragma once

#include "../IPanel.hpp"

#include <Engine/Assets/CloudTypeData.hpp>

#include <Common/Core/Core.hpp>

#include <string>
#include <vector>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor
{
    /**
     * @brief The artist's tool for a KIND of cloud: author it, look at its profile, save it as an asset.
     *
     * This is the half of the owner's request that the Details panel cannot be: a slot lets an artist
     * CHOOSE a cloud type, and this is where they MAKE one. Three things it offers that twelve sliders on
     * the component could not:
     *
     *   AUTHOR   the numbers and the vertical profile curve, validated as a set before anything is
     *            written, so an illegal type is
     *            refused with the number that is wrong rather than saved and discovered as a missing sky.
     *   SEE      the field those inputs actually produce, read down two vertical lines — at the rim of a
     *            placement patch and at its core. It is the only way to tell "this type is a tower in the
     *            middle of a patch" from "this type is flat everywhere" without rendering a frame, and it
     *            is deliberately NOT the profile curve played back: the curve is the input, and a preview
     *            that redrew the input would agree with itself no matter what the generator did with it.
     *   KEEP     save to a `.decloudtype`, which the Content Browser then lists and a cloud layer's slot
     *            then accepts. That round trip is the whole feature.
     *
     * NO BACKGROUND WORK, unlike the noise volume panel next door. The claim used to be about generating a
     * 16 384-texel profile table, which has not existed since phase Э5; what it costs now is placing one
     * preview region's lumps and reading the field down two vertical lines, which is a few thousand
     * evaluations. Still cheaper than the widgets around it, so it is redrawn every frame the panel is open.
     *
     * ONE WINDOW PER `.decloudtype`, OPENED BY DOUBLE-CLICKING THE ASSET — UE's flow, and what this class
     * became in Р3. It was a SINGLETON with an "Open a type..." combo, reached from the View menu; the
     * combo is gone with the singleton, because the subject is now the window's identity.
     */
    class CloudTypePanel final : public IAssetEditorPanel
    {
    public:
        CloudTypePanel( const Assets::AssetHandle& subject, Assets::AssetManager* assets );

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 520.0f, 760.0f );
        }

        void OnUIRender() override;

        // NEVER — see CloudNoiseVolumePanel::HoldsRendererSlot. This panel draws a curve with ImGui::PlotLines
        // and owns no Scene and no SceneRenderer, so it costs none of the six slots.
        [[nodiscard]] bool HoldsRendererSlot() const override
        {
            return false;
        }

        // ...and never will — see CloudNoiseVolumePanel::ClaimsRendererSlot.
        [[nodiscard]] bool ClaimsRendererSlot() const override
        {
            return false;
        }

    private:
        void DrawLibrarySection();
        void DrawShapeSection();

        // The vertical profile, drawn as the cloud's own silhouette and edited by dragging it.
        //
        // ITS OWN METHOD AND NOT PART OF THE SHAPE SECTION, because it is the one control here that is a
        // CANVAS rather than a slider: it owns a hit region, a drag that runs across several samples, and
        // a set of presets. Folding it into the slider list would have buried the only authored thing on
        // this panel that is not a number.
        void DrawProfileEditor();

        void DrawNoiseSection();
        void DrawPreviewSection();
        void DrawSaveSection();

        // Loads @p path into the editing buffer. Separate from the combo that calls it so that "open" is
        // one operation whether it came from the list or from a save that has just happened.
        void OpenType( const Common::Filepath& path );

        Assets::AssetManager* m_Assets = nullptr;

        Assets::CloudTypeData m_Data = Assets::CloudTypeDefault();
        Common::Filepath      m_SourcePath;                        // empty until saved or opened
        std::string           m_SourceName = "(built-in default)"; // what is in the buffer, for the header

        // The two text fields, kept as fixed buffers because ImGui::InputText writes into one and the
        // asset's own strings are std::optional<std::string>.
        char m_NameBuffer[128]  = {};
        char m_NotesBuffer[512] = {};

        std::string m_Status;
        bool        m_StatusIsError = false;

        // The preview's two curves — the profile at the rim of a placement patch and at its core. Members
        // rather than locals so the plot does not reallocate 512 floats every frame.
        std::vector<float> m_ProfileEdge;
        std::vector<float> m_ProfileCore;
    };
} // namespace Desert::Editor
