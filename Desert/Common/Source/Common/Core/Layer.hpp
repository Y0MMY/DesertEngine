#pragma once

#include <Common/Core/Events/Event.hpp>
#include <Common/Core/Timestep.hpp>
#include <Common/Core/ResultStr.hpp>

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

        [[nodiscard]] virtual Common::BoolResultStr OnAttach()                             = 0;
        [[nodiscard]] virtual Common::BoolResultStr OnDetach()                             = 0;
        [[nodiscard]] virtual Common::BoolResultStr OnUpdate( const Common::Timestep& ts ) = 0;
        [[nodiscard]] virtual Common::BoolResultStr OnImGuiRender()                        = 0;
        /*virtual void OnEvent(Event& event) = 0; */

        inline const std::string& GetName()
        {
            return m_Name;
        }

    private:
        std::string m_Name;
    };
} // namespace Common