#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Core/Formats/Shader.hpp>
#include <Engine/Core/Models/Shader.hpp>

namespace Desert::Graphic
{
    using ShaderDefines = std::vector<std::pair<std::string, std::string>>;

    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void                                           Use( BindUsage use = BindUsage::Bind ) const    = 0;
        virtual void                                           RT_Use( BindUsage use = BindUsage::Bind ) const = 0;
        virtual Common::BoolResult                             Reload()                                        = 0;
        virtual const std::string                              GetName() const                                 = 0;
        virtual const std::vector<Core::Models::UniformBuffer> GetUniformBufferModels() const                  = 0;
        virtual const std::vector<Core::Models::StorageBuffer> GetStorageBufferModels() const                  = 0;
        virtual const std::vector<Core::Models::ImageCubeSampler> GetUniformImageCubeModels() const            = 0;
        virtual const std::vector<Core::Models::Image2DSampler>   GetUniformImage2DModels() const              = 0;
        virtual const ShaderDefines&                              GetDefines() const                           = 0;
        static std::string GetStringShaderStage( const Core::Formats::ShaderStage stage );

        static std::shared_ptr<Shader> Create( const std::string& filename, const ShaderDefines& defines = {} );
    };

    class ShaderLibrary final
    {
    public:
        ShaderLibrary() = default;

        static void LoadShader( const std::shared_ptr<Shader>& shader, const ShaderDefines& defines );
        static Common::Result<std::shared_ptr<Shader>> Get( const std::string&   shaderName,
                                                            const ShaderDefines& defines );
        static inline bool                             IsShaderInLibrary( const std::string& shaderName );
        static auto&                                   GetAll()
        {
            return s_AllShaders;
        }

    private:
        static inline std::unordered_map<std::string, std::shared_ptr<Shader>> s_AllShaders;
    };
} // namespace Desert::Graphic