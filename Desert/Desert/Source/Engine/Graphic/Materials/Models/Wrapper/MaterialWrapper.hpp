#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

namespace Desert::Graphic::MaterialHelper
{
    class MaterialWrapper
    {
    public:
        explicit MaterialWrapper( const std::shared_ptr<MaterialExecutor>& baseMaterial,
                                  std::string&&                            uniformName )
             : m_MaterialExecutor( baseMaterial ), m_UniformName( std::move( uniformName ) )
        {
            m_UniformProperty = m_MaterialExecutor->GetUniformBufferProperty( m_UniformName );
        }

        virtual const std::shared_ptr<MaterialExecutor>& GetMaterialExecutor() const final
        {
            return m_MaterialExecutor;
        }

    protected:
        std::shared_ptr<MaterialExecutor>      m_MaterialExecutor;
        std::string                            m_UniformName;
        std::shared_ptr<UniformBufferProperty> m_UniformProperty;
    };
} // namespace Desert::Graphic::MaterialHelper