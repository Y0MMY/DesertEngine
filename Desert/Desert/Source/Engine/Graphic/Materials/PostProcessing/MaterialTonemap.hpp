#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    class MaterialTonemap final : public Material
    {
    public:
        explicit MaterialTonemap();

        struct Params
        {
            float Exposure;
            float Gamma;
            float BloomIntensity;
            float ExposureKey;
            bool  AutoExposure;
            float ChromaticBloom; // lens dispersion strength on the bloom halo (0 = off)
        };

        void Bind( const std::shared_ptr<Image2D>& targetImage, const std::shared_ptr<Image2D>& bloomImage,
                   const std::shared_ptr<Image2D>& avgLuminance, const Params& params );

        MPROPERTY( float, Exposure,            "u_Exposure",            1.0f )
        MPROPERTY( float, Gamma,               "u_Gamma",               2.2f )
        MPROPERTY( float, BloomIntensity,      "u_BloomIntensity",      0.0f )
        MPROPERTY( float, ExposureKey,         "u_ExposureKey",         0.18f )
        MPROPERTY( float, AutoExposureEnabled, "u_AutoExposureEnabled", 0.0f )
        MPROPERTY( float, ChromaticBloom,      "u_ChromaticBloom",      0.0f )

    private:
        Texture2DProperty* m_GeometryTexture = nullptr;
        Texture2DProperty* m_BloomTexture    = nullptr;
        Texture2DProperty* m_AvgLuminance    = nullptr;
    };
} // namespace Desert::Graphic
