#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>

#include <Common/Core/TemplateHelpers.hpp>

namespace Desert::Graphic
{
    class Material
    {
    public:
        explicit Material( std::string&& debugName, std::string&& shaderName )
             : m_MaterialExecutor(
                    Graphic::MaterialExecutor::Create( std::move( debugName ), std::move( shaderName ) ) )
        {
        }

        virtual ~Material() = default;

        virtual std::shared_ptr<MaterialExecutor> GetMaterialExecutor() const final
        {
            return m_MaterialExecutor;
        }

        bool IsDirty() const
        {
            return m_ParametersDirty;
        }

        void MarkDirty()
        {
            m_ParametersDirty = true;
        }

        void ClearDirty()
        {
            m_ParametersDirty = false;
        }

    private:
        void ProcessFieldValue( const auto& uniformProperty, const auto& fieldName, const auto& value )
        {
            const auto& field = uniformProperty->GetField( std::string( fieldName ) );
            if ( field )
            {
                if constexpr ( is_container<std::decay_t<decltype( value )>>::value )
                {
                    field->SetArray( value.data(), value.size() );
                }
                else
                {
                    field->SetValue( value );
                }
            }
        }

        void ProcessTextureField( const auto& fieldName, const auto& texture )
        {
            if constexpr ( std::is_same_v<std::decay_t<decltype( texture )>, ImageCubeRef> )
            {
                m_MaterialExecutor->GetTextureCubeProperty( std::string( fieldName ) )->SetTexture( texture );
            }
            else if constexpr ( std::is_same_v<std::decay_t<decltype( texture )>, Image2DRef> )
            {
                m_MaterialExecutor->GetTexture2DProperty( std::string( fieldName ) )->SetImage( texture );
            }
        }

    protected:
        // TODO: use MarkDirty();

        template <typename MaterialUB>
        void SetUniformValue( const MaterialUB& data )
        {
            const auto& uniformName = MaterialUB::shader_UB_name;

            auto uniformProperty             = m_MaterialExecutor->GetUniformBufferProperty( uniformName );
            bool hasUniformFields            = false;
            bool uniformBufferNotFoundLogged = false;

            data.for_each_field_read(
                 [&uniformProperty, &hasUniformFields, &uniformBufferNotFoundLogged, &uniformName,
                  this]( const auto& fieldName, const auto& rflValue )
                 {
                     const auto& value = rflValue.value();

                     if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, ImageCubeRef> ||
                                    std::is_same_v<std::decay_t<decltype( value )>, Image2DRef> )
                     {
                         ProcessTextureField(fieldName, value);
                     }
                     else
                     {
                         hasUniformFields = true;

                         if ( !uniformProperty )
                         {
                             if ( !uniformBufferNotFoundLogged )
                             {
                                 LOG_ERROR( "Uniform buffer {} not found!", uniformName );
                                 uniformBufferNotFoundLogged = true;
                             }
                             return;
                         }

                         if constexpr ( is_optional<std::decay_t<decltype( value )>>::value )
                         {
                             if ( !value.has_value() )
                             {
                                 LOG_TRACE( "Optional field {} is nullopt, skipping", fieldName );
                                 return;
                             }
                             const auto& actualValue = value.value();
                             ProcessFieldValue( uniformProperty, fieldName, actualValue );
                         }
                         else
                         {
                             ProcessFieldValue( uniformProperty, fieldName, value );
                         }
                     }
                 } );

            if ( hasUniformFields && !uniformProperty && !uniformBufferNotFoundLogged )
            {
                LOG_ERROR( "Uniform buffer {} not found!", uniformName );
            }
        }

        void SyncToGPU( const std::string& uniformName )
        {
            auto uniformProperty = m_MaterialExecutor->GetUniformBufferProperty( uniformName );
            if ( uniformProperty )
            {
                uniformProperty->UpdateFields();
            }
        }

        template <typename MaterialUB>
        void InitializeUniformBuffer()
        {
            static MaterialUB zeroData{};
            SetUniformValue( zeroData );
            LOG_TRACE( "Uniform {} initialized with default values", MaterialUB::shader_UB_name );
        }

    protected:
        std::shared_ptr<MaterialExecutor> m_MaterialExecutor;
        bool                              m_ParametersDirty = false;
    };
} // namespace Desert::Graphic