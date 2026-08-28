#pragma once

#include <memory>
#include <string>

#include <ImGui/imgui.h>

#include <Common/Core/Events/Event.hpp>

#include <Engine/Assets/Common.hpp>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor
{
    class IPanel
    {
    public:
        explicit IPanel( std::string&& panelName, bool showPanel = true )
             : m_PanelName( std::move( panelName ) ), m_SowPanel( showPanel )
        {
        }

        virtual void OnEvent( Common::Event& e ) {}
        virtual void OnPreUpdate()               {}
        virtual ~IPanel()                        = default;
        virtual void       OnUIRender()          = 0;

        // Multi-scene editing: rebind a scene-bound panel to the newly-focused scene so the Outliner,
        // Details, Settings, etc. follow whichever viewport the user is working in. No-op for panels that
        // are not tied to a specific scene (asset browser, logs, ...). See EditorLayer::SetActiveScene.
        virtual void SetScene( const std::shared_ptr<Desert::Core::Scene>& /*scene*/ )
        {
        }
        const std::string& GetName() const
        {
            return m_PanelName;
        }

        virtual void ToggleVisibility() final
        {
            m_SowPanel = !m_SowPanel;
        }

        virtual bool& GetVisibility()
        {
            return m_SowPanel;
        }

        // --- Contextual panels ---------------------------------------------------------------------
        // A tool panel is only meaningful for a particular selection or mode: the Sequencer for something
        // animatable, the Particle Editor for an emitter, Modeling for Modeling mode. Rather than hanging
        // around empty ("Select a ... to ..."), such a panel OPENS ITSELF when its context appears and
        // steps aside when it goes away — the editor shows what the work needs, not everything at once.
        //
        // Explicit intent always wins: opening a panel by hand (View menu / command palette) PINS it, and
        // a pinned panel is never auto-closed. Closing it by hand unpins it again.
        virtual bool IsContextual() const
        {
            return false;
        }

        // Is this panel's context present right now? Only consulted when IsContextual().
        virtual bool IsRelevant() const
        {
            return true;
        }

        bool& Pinned()
        {
            return m_Pinned;
        }

        // Window padding for this panel. One number for the whole editor keeps every panel's content
        // breathing the same way; the viewport overrides it to zero because its image must reach the
        // window edges. (Pushed by the panel loop around Begin/End — panels don't do it themselves.)
        virtual ImVec2 GetWindowPadding() const
        {
            return ImVec2( 8.0f, 8.0f );
        }

        // Preferred window size the FIRST time the panel ever opens (0,0 = let ImGui decide). Once
        // the user moves/resizes it, imgui.ini remembers their layout instead. Floating tool windows
        // (Node Graph, Sequencer, Build Settings) override this so they don't pop up as tiny
        // arbitrarily-placed windows.
        virtual ImVec2 GetDefaultSize() const
        {
            return ImVec2( 0.0f, 0.0f );
        }

    protected:
        const std::string m_PanelName;
        bool              m_SowPanel;
        bool              m_Pinned = false; // opened by hand: never auto-closed (see IsContextual)
    };

    // The window title an asset document must carry: "<display name>###assetdoc<subject handle>".
    //
    // THE ###id IS NOT DECORATION. Every panel is drawn with ImGui::Begin( PanelDisplayTitle( GetName() ) ),
    // and ImGui derives a window's identity from the text after the LAST "###" — so two windows whose titles
    // agree there are ONE window, merged, with the second one's content drawn into the first. Every panel
    // before this was a singleton and never met the problem; ViewportPanel is the one type that did, and it
    // escapes exactly this way ("<name>###sceneview<id>", EditorLayer::AddSceneView). This is that same
    // escape, keyed on the SUBJECT rather than on a counter, which is also what makes open-or-focus fall out
    // for free: the same material can only ever produce the same title, hence the same window.
    //
    // The subject and not a fresh id, deliberately: an id source would let one material open twice, and the
    // two windows would then edit one asset through two parameter tables.
    //
    // The display half is stripped of any "###" of its own, and that is not defensive programming: what
    // EditorLayer hands to Begin() is PanelDisplayTitle(GetName()), which appends "###" + the whole name
    // again, and ImGui takes the id from the LAST "###" in the string. A display name carrying one (an asset
    // file may be named anything) would therefore end up deciding the window id — and two such assets would
    // merge into one window, which is the exact failure this function exists to prevent.
    [[nodiscard]] inline std::string AssetDocumentTitle( const std::string&         displayName,
                                                         const Assets::AssetHandle& subject )
    {
        std::string label = displayName;
        if ( const auto pos = label.find( "###" ); pos != std::string::npos )
            label.erase( pos );

        return label + "###assetdoc" + std::to_string( static_cast<uint64_t>( subject ) );
    }

    // A panel that edits ONE asset: a document, not a tool.
    //
    // The difference that matters to the editor: a tool panel is created once at startup and toggled, so its
    // name is a constant and hiding it is the whole of "closing" it. A document is created when the user
    // opens an asset and DESTROYED when the window is dismissed — that destruction is what returns the
    // Scene, the SceneRenderer and one of the six renderer slots, and there is no way to write it as a
    // visibility flag. See EditorLayer::CloseDismissedAssetDocuments, which is the scene-view close path
    // applied to the same problem.
    class IAssetEditorPanel : public IPanel
    {
    public:
        IAssetEditorPanel( const std::string& displayName, const Assets::AssetHandle& subject,
                           Assets::AssetTypeID type )
             : IPanel( AssetDocumentTitle( displayName, subject ), /*showPanel=*/true ), m_Subject( subject ),
               m_SubjectType( type )
        {
        }

        // WHAT THIS DOCUMENT EDITS, fixed for its whole life. Immutable because it is the window's identity:
        // the title, and therefore the ImGui window id, is derived from it, and a title that changed under a
        // live window would either merge it into another document's window or orphan its saved dock entry.
        // Editing a different asset means opening a different document.
        [[nodiscard]] const Assets::AssetHandle& Subject() const noexcept
        {
            return m_Subject;
        }

        [[nodiscard]] Assets::AssetTypeID SubjectType() const noexcept
        {
            return m_SubjectType;
        }

        // Is this document holding one of the six renderer slots RIGHT NOW?
        //
        // Asked by the slot census before a seventh consumer is admitted. A document that is open but has
        // not drawn yet holds nothing and still has a claim coming, which is why the census counts pending
        // demand separately rather than trusting the live-renderer count alone.
        [[nodiscard]] virtual bool HoldsRendererSlot() const = 0;

        // Will this document EVER claim one of the six renderer slots?
        //
        // HoldsRendererSlot answers "right now"; this answers "ever", and the census needs both. It counts
        // an open document that holds no slot as PENDING DEMAND, because a Material Editor that has not
        // drawn yet is a claim that has not landed — but a document that renders on the CPU has no claim
        // coming at all, and counting it would refuse a window that costs nothing. With the four cloud
        // documents (which bake on the CPU and upload an Image2D) that is not hypothetical: five of them
        // open beside the main viewport made `live + pending` reach the cap, and the sixth was refused with
        // a census telling the user to close windows that were holding nothing.
        //
        // Defaults to TRUE, which is the conservative answer: a new document type that forgets to say is
        // treated as a claimant and refused early, rather than admitted past the cap and discovered as two
        // surfaces quietly trading each other's per-frame camera some minutes later.
        [[nodiscard]] virtual bool ClaimsRendererSlot() const
        {
            return true;
        }

    private:
        const Assets::AssetHandle m_Subject;
        const Assets::AssetTypeID m_SubjectType;
    };
} // namespace Desert::Editor