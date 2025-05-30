#pragma once

#include <Common/Core/Layer.hpp>

namespace Desert::ImGui
{
    class ImGuiLayer : public Common::Layer
    {
    public:
        virtual void Begin()                                      = 0;
        virtual void Process( const std::function<void()>& func ) = 0;
        virtual void End()                                        = 0;

        static std::shared_ptr<ImGuiLayer> Create();
    };

} // namespace Desert::ImGui