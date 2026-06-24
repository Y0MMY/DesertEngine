#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic
{
    // Eye-adaptation pass: reads the scene HDR + the previous frame's 1x1 luminance, writes the new
    // adapted luminance. Adaptation params (dt, speed, min/max luma) go via push constant.
    class MaterialAutoExposure final : public Material
    {
    public:
        MaterialAutoExposure();

        struct Params
        {
            float DeltaTime;
            float AdaptSpeed;
            float MinLuma;
            float MaxLuma;
        };

        void Bind( const Image2D* scene, const Image2D* prevLuminance, const Params& params );

    private:
        Texture2DProperty* m_SceneTexture = nullptr;
        Texture2DProperty* m_PrevLuminance = nullptr;
    };
} // namespace Desert::Graphic
