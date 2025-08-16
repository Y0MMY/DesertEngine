#include "Sandbox.hpp"
#include "EditorLayer.hpp"

namespace Desert
{

    Sandbox::Sandbox( const Engine::ApplicationInfo& appinfo ) : Engine::Application( appinfo )
    {
    }

    void Sandbox::OnCreate()
    {
        PushLayer( new Editor::EditorLayer( this, "" ) );
    }

    void Sandbox::OnDestroy()
    {
    }

} // namespace Desert