#pragma once

#include <Engine/Graphic/Texture.hpp>
#include <Engine/Runtime/ImageHandle.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Graphic/SkySettings.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    struct Environment
    {
        Common::Filepath     Filepath; // TODO: Asset Env
        Runtime::ImageHandle RadianceMap;
        Runtime::ImageHandle IrradianceMap;
        Runtime::ImageHandle PreFilteredMap;

        operator bool() const
        {
            return RadianceMap.IsValid() && IrradianceMap.IsValid() && PreFilteredMap.IsValid();
        }
    };

    class EnvironmentManager
    {
    public:
        static Environment Create( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset );

        // Builds an IBL environment from the engine-generated procedural atmosphere (no HDR asset): the
        // sky is baked into an equirect panorama, then run through the same radiance/irradiance/prefilter
        // pipeline. sunDir is the direction TOWARD the sun (normalized).
        static Environment CreateProcedural( const glm::vec3& sunDir, float intensity, float diskRadius,
                                             const SkySettings& sky );

    private:
        static std::shared_ptr<ImageCube>
        ConvertPanoramaToCubemapCross( const Runtime::ImageHandle& panorama );

        static std::shared_ptr<ImageCube>
        CreateDiffuseIrradiance( const Runtime::ImageHandle& panorama );

        // GGX-prefilters an already-built radiance cubemap (per-mip roughness).
        static std::shared_ptr<ImageCube>
        CreatePrefilteredMap( const Runtime::ImageHandle& radianceCube );
    };
} // namespace Desert::Graphic