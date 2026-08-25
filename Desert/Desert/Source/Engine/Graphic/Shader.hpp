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

        // False when this shader has never compiled successfully, i.e. it carries no stages at all.
        //
        // A failed RECOMPILE is already safe — CompileProgram builds into locals and keeps the previous
        // modules — but a shader whose FIRST compile fails is still constructed, still registered under
        // its name, and still handed out by ShaderService. Building a pipeline from it produces
        // `stageCount = 0`, which Vulkan answers with a validation storm and a crash before the frame is
        // presented. That is an artist's typo in a shader graph, so the engine has to survive it: callers
        // ask this and skip, and the pipeline builder refuses as the last line of defence.
        [[nodiscard]] virtual bool IsCompiled() const = 0;

        static std::string             GetStringShaderStage( const Core::Formats::ShaderStage stage );
        // passName selects a `Pass "Name"` block of a DSL multi-pass shader; empty = the default
        // program. Pass shaders are named "<Shader>/<Pass>".
        static std::shared_ptr<Shader> Create( const Assets::Asset<Assets::ShaderAsset>& asset,
                                               const ShaderDefines&                      defines  = {},
                                               const std::string&                        passName = {} );
    };

} // namespace Desert::Graphic