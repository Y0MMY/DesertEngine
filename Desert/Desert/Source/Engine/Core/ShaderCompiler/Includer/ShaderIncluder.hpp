#pragma once

#include <Engine/Core/Formats/Shader.hpp>

#include <shaderc.hpp>

namespace Desert::Core
{
    class ShaderIncluder final : public shaderc::CompileOptions::IncluderInterface
    {
    public:
        explicit ShaderIncluder( const Common::Filepath& basePath );

        shaderc_include_result* GetInclude( const char* requested_source, shaderc_include_type type,
                                            const char* requesting_source, size_t include_depth ) override;

        void ReleaseInclude( shaderc_include_result* data ) override;

    private:
        shaderc_include_result* CreateErrorIncludeResult( const std::string& error );

    private:
        Common::Filepath m_BasePath;
    };
} // namespace Desert::Core