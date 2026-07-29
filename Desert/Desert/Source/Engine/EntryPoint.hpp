#pragma once

#include <Common/Core/JobSystem.hpp>
#include <Engine/Core/EngineContext.hpp>

extern Desert::Engine::Application* CreateApplication( int argc, char** argv );

int main( int argc, char** argv )
{
    Common::Logger::LogInit();

    auto app = CreateApplication( argc, argv );
    app->OnCreate();
    app->Run();
    app->OnDestroy();

    // Ordered teardown BEFORE static destructors run (their cross-TU order is undefined):
    // 1) join the worker pool while the logger/engine objects its jobs touch are still alive;
    // 2) drain the GPU so descriptor pools/buffers destroyed during static teardown are no longer
    //    referenced by in-flight command buffers (the exit-time VUID-vkDestroyDescriptorPool-00303
    //    followed by worker threads aborting on destroyed mutexes).
    Common::JobSystem::Get().Shutdown();
    if ( const auto device = Desert::EngineContext::GetInstance().GetDevice() )
        device->WaitIdle();

    return 0;
}