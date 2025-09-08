#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

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

            if constexpr ( has_data_method_v<MaterialUB> )
            {
                using ValueType = typename MaterialUB::value_type;
                static std::vector<ValueType> zeroData( 1 );
                m_UniformProperty->SetData( zeroData.data(), zeroData.size() * sizeof( ValueType ) );
            }
            else if constexpr ( has_iterators_v<MaterialUB> )
            {
                using ValueType = typename MaterialUB::value_type;
                static std::vector<ValueType> zeroData( 1 );
                m_UniformProperty->SetData( zeroData.data(), zeroData.size() * sizeof( ValueType ) );
            }
            else
            {
                static MaterialUB zeroData{};
                m_UniformProperty->SetData( &zeroData, sizeof( MaterialUB ) );
            }

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

            if constexpr ( has_data_method_v<MaterialUB> )
            {
                if ( !m_Data.empty() )
                {
                    using ValueType = typename MaterialUB::value_type;
                    m_UniformProperty->SetData( m_Data.data(), m_Data.size() * sizeof( ValueType ) );
                }
            }
            else if constexpr ( has_iterators_v<MaterialUB> )
            {
                if ( !m_Data.empty() )
                {
                    using ValueType = typename MaterialUB::value_type;
                    std::vector<ValueType> tempBuffer( m_Data.begin(), m_Data.end() );
                    m_UniformProperty->SetData( tempBuffer.data(), tempBuffer.size() * sizeof( ValueType ) );
                }
            }
            else
            {
                m_UniformProperty->SetData( &m_Data, sizeof( MaterialUB ) );
            }
        }

    private:
        template <typename T, typename = void>
        struct has_data_method : std::false_type
        {
        };

        template <typename T>
        struct has_data_method<
             T, std::void_t<decltype( std::declval<T>().data() ), decltype( std::declval<T>().size() )>>
             : std::true_type
        {
        };

        template <typename T, typename = void>
        struct has_iterators : std::false_type
        {
        };

        template <typename T>
        struct has_iterators<
             T, std::void_t<decltype( std::declval<T>().begin() ), decltype( std::declval<T>().end() ),
                            decltype( std::declval<T>().size() )>> : std::true_type
        {
        };

        template <typename T>
        static constexpr bool has_data_method_v = has_data_method<T>::value;

        template <typename T>
        static constexpr bool has_iterators_v = has_iterators<T>::value;

        MaterialUB m_Data;

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