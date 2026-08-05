#pragma once

#include <memory>
#include <string>

#include <ImGui/imgui.h>

#include <Common/Core/Events/Event.hpp>

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
} // namespace Desert::Editor