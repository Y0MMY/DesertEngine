#pragma once

#include <Engine/Graphic/SceneRenderer.hpp>

namespace Desert::Graphic::Render
{
    struct RenderCommand
    {
        virtual ~RenderCommand()                                 = default;
        virtual void Execute( Graphic::SceneRenderer& renderer ) = 0;
    };

} // namespace Desert::Graphic::Render