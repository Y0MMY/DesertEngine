#pragma once

#include "../IPanel.hpp"

#include <Engine/Assets/CloudNoiseVolume.hpp>

#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <string>

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
     * @brief The artist's tool for the volumetric clouds' 3D noise: author it, look at it, save it.
     *
     * Three things a volume that only ever existed on the GPU could not offer, and the reason this panel
     * exists rather than four sliders on the component:
     *
     *   AUTHOR   the seed, the curl strength and the four lattice periods, validated against the resolution
     *            before anything is baked, so an illegal set is refused with the number that is wrong
     *            rather than producing a volume with a seam in it.
     *   SEE      a 3D field has no picture. It has SLICES, so the panel shows one at a time, along any
     *            axis, either as all four channels at once or one channel on its own — which is the only
     *            way to tell "the billowy channel is empty" from "the wispy channel is too strong".
     *   KEEP     bake, then save to a `.dcnv`, which the Content Browser then lists and the cloud
     *            component's slot then accepts. That round trip is the whole feature.
     *
     * THE BAKE RUNS OFF THE UI THREAD. 128^3 is 8.4 million samples and measures 9.6 s optimised and 82 s
     * unoptimised on the machine this was written on; blocking the editor for that would make the tool feel
     * broken the first time it was used. The generator writes a progress fraction that the panel reads each
     * frame, and the panel refuses to start a second bake while one is running.
     *
     * ONE WINDOW PER `.dcnv`, OPENED BY DOUBLE-CLICKING THE ASSET — UE's flow, and what this class became
     * in Р3. It was a SINGLETON with an "Open" combo in it: one window, whichever volume was last picked in
     * it, reached from the View menu. The combo is gone with the singleton — the subject is fixed at
     * construction, two volumes are two windows, and the browser is where a volume is chosen. See
     * Editor/Core/AssetEditorRegistry.hpp for the seam and Clouds/CloudDocumentOpen.hpp for the resolution.
     */
    class CloudNoiseVolumePanel final : public IAssetEditorPanel
    {
    public:
        CloudNoiseVolumePanel( const Assets::AssetHandle& subject, Assets::AssetManager* assets );
        ~CloudNoiseVolumePanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 560.0f, 720.0f );
        }

        void OnUIRender() override;

        // NEVER, and this is measured rather than assumed. This panel owns no PreviewViewport, no Scene and
        // no SceneRenderer: it bakes on the CPU and uploads the result as a single Graphic::Image2D, which
        // is a device image and not a renderer. So the six-slot census (EditorLayer::RendererSlotCensus)
        // counts this document at zero for its whole life, and closing it returns nothing because it took
        // nothing. The same is true of the other three cloud documents.
        [[nodiscard]] bool HoldsRendererSlot() const override
        {
            return false;
        }

        // ...and never will, which is the half the SLOT CENSUS needs. Without it an open cloud document
        // counts as a claim that has not landed yet, and five of them would exhaust the cap on paper.
        [[nodiscard]] bool ClaimsRendererSlot() const override
        {
            return false;
        }

    private:
        // Reads the subject out of the AssetManager into the editing buffer. Called once, from the
        // constructor: the subject cannot change, so neither can the answer.
        void LoadSubject( Assets::AssetManager* assets );

        void DrawGenerateSection();
        void DrawPreviewSection();
        void DrawSaveSection();

        // Rebuilds the slice texture from m_Volume. Called when the volume, the axis, the slice index or
        // the channel view changes — never per frame, because it allocates a device image.
        void RefreshSlice();

        // Copies one slice of the volume into RGBA8 the way the current channel view asks for it. Pure and
        // small, so the preview cannot be a different reading of the bytes than the numbers beside it.
        std::vector<unsigned char> BuildSlicePixels() const;

        Assets::AssetManager* m_Assets = nullptr;

        Assets::CloudNoiseVolumeParams m_Params;
        Assets::CloudNoiseVolumeData   m_Volume;
        bool                           m_HasVolume = false;
        std::string                    m_SourceName; // what is in m_Volume: a file name, or "(baked)"
        std::string                    m_Status;     // the last thing that happened, shown to the artist
        bool                           m_StatusIsError = false;

        // WHERE SAVE WRITES: the subject's own file, resolved once at construction. Save As may write
        // ELSEWHERE, but it never assigns to this — a copy becomes its OWN document rather than repointing
        // this window, because the subject is the window's identity. See DrawSaveSection.
        std::filesystem::path m_SubjectPath;

        // The running bake. A future rather than a raw thread so the result is collected exactly once and
        // the panel cannot be destroyed while a thread is still writing into it.
        std::future<Common::ResultStr<Assets::CloudNoiseVolumeData>> m_Baking;
        std::atomic<float>                                           m_BakeProgress{ 0.0f };
        bool                                                         m_BakeRunning = false;

        // Preview state.
        enum class SliceAxis : int
        {
            X = 0,
            Y = 1,
            Z = 2
        };
        enum class ChannelView : int
        {
            AllFour  = 0, // R and G in red/green, B and A in blue/alpha-as-grey — the whole volume at once
            WispyLF  = 1,
            WispyHF  = 2,
            BillowLF = 3,
            BillowHF = 4
        };

        SliceAxis   m_Axis        = SliceAxis::Z;
        int         m_SliceIndex  = 0;
        ChannelView m_ChannelView = ChannelView::AllFour;
        int         m_PreviewZoom = 3; // integer magnification, so a 128-wide slice is readable

        std::shared_ptr<Graphic::Image2D> m_SliceImage;
        bool                              m_SliceDirty = true;
    };
} // namespace Desert::Editor
