#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Pipeline.hpp>

#include <Common/Core/Memory/Buffer.hpp>

#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Engine/Graphic/Materials/Properties/Texture2DProperty.hpp>
#include <Engine/Graphic/Materials/Properties/TextureCubeProperty.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    class Material
    {
    public:
        Material( std::string&& debugName, const std::shared_ptr<Shader>& shader,
                  std::unique_ptr<MaterialBackend>&& materialBackend );

        virtual ~Material() = default;

        const auto& GetUniformBufferProperties() const
        {
            return m_UniformBufferPropertiesLookup;
        }
        const auto& GetTexture2DProperties() const
        {
            return m_Texture2DPropertiesLookup;
        }
        const auto& GetTextureCubeProperties() const
        {
            return m_TextureCubePropertiesLookup;
        }

        const auto& GetPushConstantBuffer() const
        {
            return m_PushConstantBuffer;
        }

        // NOTE:temporary solution! in the future it is worth getting when parsing
        void PushConstant( const void* buffer, const uint32_t bufferSize )
        {
            m_PushConstantBuffer.Write( buffer, bufferSize );
        }

        std::shared_ptr<UniformBufferProperty> GetUniformBufferProperty( const std::string& name ) const;
        std::shared_ptr<Texture2DProperty>     GetTexture2DProperty( const std::string& name ) const;
        std::shared_ptr<TextureCubeProperty>   GetTextureCubeProperty( const std::string& name ) const;

        void                    Apply();
        std::shared_ptr<Shader> GetShader() const
        {
            return m_Shader;
        }

        static std::shared_ptr<Material> Create( std::string&& debugName, const std::shared_ptr<Shader>& shader );

    protected:
        void InitializeProperties();

    private:
        std::string                      m_DebugName;
        std::unique_ptr<MaterialBackend> m_MaterialBackend;
        std::shared_ptr<Shader>          m_Shader;

    protected:
        template <typename T>
        using PropertyStorage = std::vector<std::shared_ptr<T>>;

        template <typename T>
        using PropertyLookup = std::unordered_map<std::string, size_t>;

        Common::Memory::Buffer m_PushConstantBuffer;

        // Compact storage
        PropertyStorage<UniformBufferProperty> m_UniformBufferPropertiesStorage;
        PropertyStorage<Texture2DProperty>     m_Texture2DPropertiesStorage;
        PropertyStorage<TextureCubeProperty>   m_TextureCubePropertiesStorage;

        // Fast lookup
        PropertyLookup<UniformBufferProperty> m_UniformBufferPropertiesLookup;
        PropertyLookup<Texture2DProperty>     m_Texture2DPropertiesLookup;
        PropertyLookup<TextureCubeProperty>   m_TextureCubePropertiesLookup;
    };
} // namespace Desert::Graphic