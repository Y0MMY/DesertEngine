#pragma once

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <Engine/Graphic/Materials/MaterialReflection.hpp>

#include <Engine/Graphic/Materials/Models/Mesh/PBR/PBRConstants.hpp>

namespace Desert::Graphic::Models::PBR
{
    // clang-format off
    RFL_UB_TYPE(PBRTextures, "PBRTextures",
        FIELD_ATTR(ImageCubeRef, u_EnvIrradianceTex, "Irradiance Map",
        .texture("Cube Maps (*.hdr;*.dds)", false)
        .category("PBR Textures")
        .description("Diffuse irradiance environment map"))

        FIELD_ATTR(ImageCubeRef, u_EnvSpecularTex, "Prefiltered Map",
        .texture("Cube Maps (*.hdr;*.dds)", false)
        .category("PBR Textures")
        .description("Specular pre-filtered environment map"))

        FIELD_ATTR(std::shared_ptr<Image2D>, u_BRDFLUTTexture, "BRDF LUT",
        .texture("LUT Images (*.png;*.jpg)", false)
        .category("PBR Textures")
        .description("BRDF integration lookup texture")
        .editable(false))
    )
    // clang-format on
} // namespace Desert::Graphic::Models::PBR