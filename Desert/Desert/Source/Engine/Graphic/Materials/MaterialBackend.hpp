#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Pipeline.hpp>

namespace Desert::Graphic
{
    class MaterialExecutor;
    class MaterialProperty;

    class MaterialBackend
    {
    public:
        virtual ~MaterialBackend() = default;

        virtual void ApplyUniformBuffer( MaterialProperty* prop ) = 0;
        virtual void ApplyTexture2D( MaterialProperty* prop )     = 0;
        virtual void ApplyTextureCube( MaterialProperty* prop )   = 0;

        virtual void ApplyProperties( MaterialExecutor* material )                        = 0;
        virtual void ApplyPushConstants( MaterialExecutor* material, Pipeline* pipeline ) = 0;
    };
} // namespace Desert::Graphic