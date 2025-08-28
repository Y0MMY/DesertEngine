#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

namespace Desert::Graphic::MaterialHelper
{
    template <typename MaterialUB>
    class MaterialWrapper
    {
    public:
        explicit MaterialWrapper( const std::shared_ptr<MaterialExecutor>& baseMaterial,
                                  std::string&&                            uniformName )
             : m_MaterialExecutor( baseMaterial ), m_UniformName( std::move( uniformName ) )
        {
            m_UniformProperty = m_MaterialExecutor->GetUniformBufferProperty( m_UniformName );
        }

        virtual ~MaterialWrapper() = default;

        virtual const std::shared_ptr<MaterialExecutor>& GetMaterialExecutor() const final
        {
            return m_MaterialExecutor;
        }

        virtual void Update( const MaterialUB& props ) final
        {
            m_Data = props;
            m_UniformProperty->SetData( &m_Data, sizeof( MaterialUB ) );
        }

    private:
        MaterialUB m_Data;

    protected:
        std::shared_ptr<MaterialExecutor>      m_MaterialExecutor;
        std::string                            m_UniformName;
        std::shared_ptr<UniformBufferProperty> m_UniformProperty;
    };
} // namespace Desert::Graphic::MaterialHelper

#define DEFINE_MATERIAL_WRAPPER( className, templateParam, stringParam )                                          \
    class className final : public Desert::Graphic::MaterialHelper::MaterialWrapper<templateParam>                \
    {                                                                                                             \
    public:                                                                                                       \
        explicit className( const std::shared_ptr<Desert::Graphic::MaterialExecutor>& material )                  \
             : MaterialWrapper( material, ##stringParam )                                                          \
        {                                                                                                         \
        }                                                                                                         \
    };