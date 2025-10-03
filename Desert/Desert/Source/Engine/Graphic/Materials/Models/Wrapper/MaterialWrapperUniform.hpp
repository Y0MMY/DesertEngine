#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

#include <Common/Core/TemplateHelpers.hpp>

#include <rflcpp/rfl.hpp>

namespace Desert::Graphic::MaterialHelper
{
#define LOG_UNIFORM_ALLOW 1
#if LOG_UNIFORM_ALLOW
#define LOG_UNIFORM( ... ) LOG_TRACE( __VA_ARGS__ )
#else
#define LOG_UNIFORM( ... )
#endif
    template <typename MaterialUB>
    class MaterialWrapperUniform
    {
    public:
        explicit MaterialWrapperUniform( const std::shared_ptr<MaterialExecutor>& baseMaterial,
                                         std::string&&                            uniformName )
             : m_MaterialExecutor( baseMaterial ), m_UniformName( std::move( uniformName ) )
        {
            m_UniformProperty = m_MaterialExecutor->GetUniformBufferProperty( m_UniformName );
            if ( !m_UniformProperty )
            {
                LOG_ERROR( "The uniform {} was not found!", m_UniformName );
            }

            static MaterialUB zeroData{};
            UpdateUniform( zeroData );

            LOG_UNIFORM( "Uniform {} initialized with default values", m_UniformName );
        }

        virtual ~MaterialWrapperUniform() = default;

        virtual const std::shared_ptr<MaterialExecutor>& GetMaterialExecutor() const final
        {
            return m_MaterialExecutor;
        }

        virtual void Update( const MaterialUB& props ) final
        {
            if ( !m_UniformProperty )
            {
                LOG_UNIFORM( "Update was skipped! The uniform {} was not found!", m_UniformName );
                return;
            }
            m_Data = props;
            UpdateUniform( props );
        }

    private:
        void UpdateUniform( const MaterialUB& data )
        {
            const auto dataReflected = rfl::to_named_tuple( data );
            dataReflected.apply(
                 [this]( const auto& f )
                 {
                     auto        field_name = f.name();
                     const auto& value      = f.value();

                     if constexpr ( is_container<std::decay_t<decltype( value )>>::value )
                     {
                         m_UniformProperty->GetField( std::string( field_name ) )
                              ->SetArray( value.data(), value.size() );
                     }

                     else
                     {
                         m_UniformProperty->GetField( std::string( field_name ) )->SetValue( value );
                     }
                 } );

            m_UniformProperty->UpdateFields();
        }

    private:
        MaterialUB m_Data{};

    protected:
        std::shared_ptr<MaterialExecutor>      m_MaterialExecutor;
        std::string                            m_UniformName;
        std::shared_ptr<UniformBufferProperty> m_UniformProperty;
    };
} // namespace Desert::Graphic::MaterialHelper

#define DEFINE_MATERIAL_WRAPPER_UNIFORM( className, templateParam, stringParam )                                  \
    class className final : public Desert::Graphic::MaterialHelper::MaterialWrapperUniform<templateParam>         \
    {                                                                                                             \
    public:                                                                                                       \
        explicit className( const std::shared_ptr<Desert::Graphic::MaterialExecutor>& material )                  \
             : MaterialWrapperUniform( material, ##stringParam )                                                  \
        {                                                                                                         \
        }                                                                                                         \
    };