#pragma once

#include <Engine/Runtime/ImageHandle.hpp>

namespace Desert::Graphic::ShaderProtocols
{
    struct PBRTexturesUB
    {
        inline const static std::string NameIrradiance = "u_EnvIrradianceTex";
        inline const static std::string NameSpecular   = "u_EnvSpecularTex";
        inline const static std::string NameBRDF       = "u_BRDFLUTTexture";

        std::optional<Runtime::ImageHandle> EnvIrradiance;
        std::optional<Runtime::ImageHandle> EnvSpecular;
        std::optional<Runtime::ImageHandle> BRDFLUTT;
    };
} // namespace Desert::Graphic::ShaderProtocols
