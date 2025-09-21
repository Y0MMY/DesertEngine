#pragma once

#include <Common/Core/Reflection.hpp>

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

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
            else
            {
                InitializeDefaultValues();

                LOG_UNIFORM( "Uniform {} initialized with default values", m_UniformName );
            }
        }

        virtual ~MaterialWrapperUniform() = default;

        virtual const std::shared_ptr<MaterialExecutor>& GetMaterialExecutor() const final
        {
            return m_MaterialExecutor;
        }

        /* virtual const auto& GetWritableData() const final
         {
             return m_Data;
         }*/

        virtual void Update( const MaterialUB& props ) final
        {
            if ( !m_UniformProperty )
            {
                LOG_UNIFORM( "Update was skipped! The uniform {} was not found!", m_UniformName );
                return;
            }
            m_Data = props;

            UpdateUniformData( m_Data );
        }

    private:
        void InitializeDefaultValues()
        {
            if constexpr (is_container<MaterialUB>::value || is_reflected<MaterialUB>::value)
            {
                using ValueType = typename MaterialUB::value_type;
                std::vector<ValueType> zeroData( 1 );
                m_UniformProperty->SetData( zeroData.data(), zeroData.size() * sizeof( ValueType ) );
            }

            else
            {
                MaterialUB zeroData{};
                m_UniformProperty->SetData( &zeroData, sizeof( MaterialUB ) );
            }
        }

        void UpdateUniformData( const MaterialUB& props )
        {

            if constexpr ( is_container<MaterialUB>::value)
            {
                if ( !props.empty() )
                {
                    using ValueType = typename MaterialUB::value_type;
                    m_UniformProperty->SetData( props.data(), props.size() * sizeof( ValueType ) );
                }
            }
            else if constexpr ( is_reflected<MaterialUB>::value) // reflected struct
            {
                m_UniformProperty->SetData( props.data(), props.size() );
            }
            else
            {
                m_UniformProperty->SetData( &props, sizeof( MaterialUB ) );
            }
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