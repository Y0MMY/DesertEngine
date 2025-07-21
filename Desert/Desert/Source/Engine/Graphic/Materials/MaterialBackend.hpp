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
        explicit MaterialBackend( const std::shared_ptr<Shader>& shader ) : m_Shader( shader )
        {
        }

        virtual ~MaterialBackend() = default;

        virtual void ApplyUniformBuffer( MaterialProperty* prop ) = 0;
        virtual void ApplyTexture2D( MaterialProperty* prop )     = 0;
        virtual void ApplyTextureCube( MaterialProperty* prop )   = 0;

        virtual void FlushUpdates() = 0;

        virtual void ApplyPushConstants( MaterialExecutor* material, Pipeline* pipeline ) = 0;

    protected:
        const std::shared_ptr<Shader> m_Shader;
    };
} // namespace Desert::Graphic