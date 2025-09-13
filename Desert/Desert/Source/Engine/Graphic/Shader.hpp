#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Core/Formats/Shader.hpp>
#include <Engine/Core/Models/Shader.hpp>

#include <Engine/Assets/Shader/ShaderAsset.hpp>

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
        virtual const Common::Filepath&                           GetFilepath() const                          = 0;

        static std::string             GetStringShaderStage( const Core::Formats::ShaderStage stage );
        static std::shared_ptr<Shader> Create( const Assets::Asset<Assets::ShaderAsset>& asset,
                                               const ShaderDefines&                      defines = {} );
    };

} // namespace Desert::Graphic