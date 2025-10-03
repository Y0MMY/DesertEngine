#pragma once

#include <Engine/Desert.hpp>

namespace Desert::Editor::Render
{
    class IRender
    {
    public:
        explicit IRender( const std::shared_ptr<Desert::Core::Scene>& scene ) : m_DstScene( scene ) {};
        virtual ~IRender() = default;

        virtual Common::BoolResultStr Init() = 0;
        virtual void Render() = 0;
    protected:
        std::weak_ptr<Desert::Core::Scene> m_DstScene;
    };
} // namespace Desert::Editor::Render