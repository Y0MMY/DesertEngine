#pragma once

#include <Engine/Graphic/Clouds/CloudEnvironmentBake.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Runtime/ImageHandle.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>

#include <glm/glm.hpp>

namespace Desert::ShaderResources
{
    class StorageBuffer;
}

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

        // Builds an IBL environment from the engine-generated procedural atmosphere (no HDR asset): the sky
        // is baked into an equirect panorama of @p panoramaWidth x @p panoramaHeight, then run through the
        // same radiance/irradiance/prefilter pipeline. @p skyParams is the caller's sky parameter buffer —
        // the same one the screen sky pass reads.
        //
        // @p transmittanceLut / @p multiScatterLut are the cached atmosphere LUTs the bake's physical
        // branch (SkyModel::PhysicalAtmosphere) marches with; the caller guarantees they hold valid
        // texels when the payload's model lane says physical. Pass nullptr on the gradient model — the
        // bake then binds fallbacks and the physical branch is never taken.
        //
        // @p clouds is the caller's cloud layer, marched into the SAME panorama by the SAME field the
        // screen pass marches — see Engine/Graphic/Clouds/CloudEnvironmentBake.hpp for why the argument
        // exists at all: this call is the only route from the clouds to the bake, and the alternative was
        // a second, analytic model of the clouds standing beside the march.
        //
        // The panorama size is authored (SkyAtmosphereData::EnvironmentResolution) rather than a constant
        // because this cost is paid PER LIVE SceneRenderer, and the editor keeps several of those.
        static Environment CreateProcedural( uint32_t panoramaWidth, uint32_t panoramaHeight,
                                             ShaderResources::StorageBuffer* skyParams, Image2D* transmittanceLut,
                                             Image2D* multiScatterLut, const CloudBakeBinding& clouds );

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