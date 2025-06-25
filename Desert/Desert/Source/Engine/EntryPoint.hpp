#pragma once

extern Desert::Engine::Application* CreateApplication( int argc, char** argv );

int main( int argc, char** argv )
{
    Common::Logger::LogInit();

    auto app = CreateApplication( argc, argv );
   // try
   // {
        app->Run();
   // }
   // catch ( const std::exception& e )
   // {
    //    LOG_CRITICAL( "{}", e.what() );
   // }

    return 0;
}