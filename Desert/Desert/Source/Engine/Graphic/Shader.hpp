#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Core/Formats/Shader.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>

#include <Engine/ShaderResources/ShaderReflectionTypes.hpp>

#include <Engine/Assets/Shader/ShaderAsset.hpp>

namespace Desert::Graphic
{
    using ShaderDefines = std::vector<std::pair<std::string, std::string>>;

    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void                  Use( BindUsage use = BindUsage::Bind ) const                             = 0;
        virtual void                  RT_Use( BindUsage use = BindUsage::Bind ) const                          = 0;
        virtual Common::BoolResultStr Reload()                                                                 = 0;
        virtual const std::string     GetName() const                                                          = 0;
        virtual const std::vector<ShaderResources::ShaderLayout::UniformBuffer> GetUniformBufferModels() const = 0;
        virtual const std::vector<ShaderResources::ShaderLayout::StorageBuffer> GetStorageBufferModels() const = 0;
        virtual const std::vector<ShaderResources::ShaderLayout::ImageCubeSampler>
        GetUniformImageCubeModels() const = 0;
        virtual const std::vector<ShaderResources::ShaderLayout::Image2DSampler>
                                        GetUniformImage2DModels() const = 0;
        virtual const ShaderDefines&    GetDefines() const              = 0;
        virtual const Common::Filepath& GetFilepath() const             = 0;

        // Data-driven material metadata parsed from the .shader's `#pragma param` / `#pragma state`.
        virtual const Core::Formats::ShaderProgramMeta& GetProgramMeta() const = 0;

        static std::string             GetStringShaderStage( const Core::Formats::ShaderStage stage );
        // passName selects a `Pass "Name"` block of a DSL multi-pass shader; empty = the default
        // program. Pass shaders are named "<Shader>/<Pass>".
        static std::shared_ptr<Shader> Create( const Assets::Asset<Assets::ShaderAsset>& asset,
                                               const ShaderDefines&                      defines  = {},
                                               const std::string&                        passName = {} );
    };

} // namespace Desert::Graphic