#pragma once

#include <Engine/Desert.hpp>
#include <Engine/EntryPoint.hpp>

using namespace Desert::Engine;
Desert::Engine::Applicaton* CreateApplication( int argc, char** argv )
{
    ApplicationInfo appInfo;
    appInfo.Title = "Desert Engine";

    Common::Singleton<Applicaton>::CreateInstance( appInfo );
    return &Common::Singleton<Applicaton>::GetInstance();
}
