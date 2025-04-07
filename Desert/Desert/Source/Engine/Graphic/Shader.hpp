#pragma once

#include <Engine/Graphic/RendererTypes.hpp>
#include <Engine/Core/Formats/Shader.hpp>

namespace Desert::Graphic
{
    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void               Use( BindUsage use = BindUsage::Bind ) const    = 0;
        virtual void               RT_Use( BindUsage use = BindUsage::Bind ) const = 0;
        virtual Common::BoolResult Reload()                                        = 0;
        virtual const std::string  GetName() const                                 = 0;

        static std::shared_ptr<Shader> Create( const std::string& filename );
    };

    class ShaderLibrary final
    {
    public:
        ShaderLibrary() = default;

        static void                                    LoadShader( const std::shared_ptr<Shader>& shader );
        static Common::Result<std::shared_ptr<Shader>> Get( const std::string& shaderName );
        static inline bool IsShaderInLibrary(const std::string& shaderName);


    private:
        static inline std::unordered_map<std::string, std::shared_ptr<Shader>> s_AllShaders;
    };
} // namespace Desert::Graphic