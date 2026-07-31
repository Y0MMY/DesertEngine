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
    };
} // namespace Desert::Editor