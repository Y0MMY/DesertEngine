#pragma once

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>

namespace Desert
{
    class Sandbox : public Engine::Application
    {
    public:
        Sandbox( const Engine::ApplicationInfo& appinfo );

        virtual void OnCreate() override;
        virtual void OnDestroy() override;
    };
} // namespace Desert

Desert::Engine::Application* CreateApplication( int argc, char** argv )
{
    using namespace Desert::Engine;

    ApplicationInfo appInfo;
    appInfo.Title  = "Desert Engine";
    appInfo.Width  = 1920;
    appInfo.Height = 780;
    appInfo.Fullscreen = false;

    return &Common::Singleton<Desert::Sandbox>::CreateInstance( appInfo );
}
