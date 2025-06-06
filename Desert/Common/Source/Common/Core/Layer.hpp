#pragma once

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/Timestep.hpp>
#include <Common/Core/Result.hpp>

#include <string>

namespace Common
{
    class Layer
    {
    public:
        Layer( const std::string& name = "DebugName" );
        virtual ~Layer()
        {
        }

        [[nodiscard]] virtual Common::BoolResult OnAttach()                             = 0;
        [[nodiscard]] virtual Common::BoolResult OnDetach()                             = 0;
        [[nodiscard]] virtual Common::BoolResult OnUpdate( const Common::Timestep& ts ) = 0;
        [[nodiscard]] virtual Common::BoolResult OnImGuiRender()                        = 0;
        /*virtual void OnEvent(Event& event) = 0; */

        inline const std::string& GetName()
        {
            return m_Name;
        }

    private:
        std::string m_Name;
    };
} // namespace Common