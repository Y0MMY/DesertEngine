#include "Sandbox.hpp"
#include "EditorLayer.hpp"

namespace Desert
{

    Sandbox::Sandbox( const Engine::ApplicationInfo& appinfo ) : Engine::Application( appinfo )
    {
    }

    void Sandbox::OnCreate()
    {
        PushLayer(new EditorLayer(m_Window, ""));
    }

    void Sandbox::OnDestroy()
    {
    }

} // namespace Desert