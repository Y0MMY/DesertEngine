#pragma once

#include <Common/Core/AutoRegistry.hpp>
#include <Common/Core/Events/Event.hpp>

namespace Common
{
    class EventHandler : public AutoRegistry<EventHandler>
    {
    public:
        virtual void OnEvent( Common::Event& e ) = 0;
    };
} // namespace Common