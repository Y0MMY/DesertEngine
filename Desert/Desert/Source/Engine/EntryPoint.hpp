#pragma once

extern Desert::Engine::Application* CreateApplication( int argc, char** argv );

int main( int argc, char** argv )
{
    Common::Logger::LogInit();

    auto app = CreateApplication( argc, argv );
    app->Run();

    return 0;
}