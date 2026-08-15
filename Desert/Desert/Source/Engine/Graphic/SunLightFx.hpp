#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // The atmosphere sun light's RENDER EFFECTS, in transport form — the evaluated slice of
    // ECS::DirectionalLightData that the renderer consumes beyond plain surface illumination. Travels
    // on the ProceduralSkyCommand next to the sun direction, because every field here is a property OF
    // that sun: shafts without the sun that casts them are not a thing, and neither is a volumetric
    // scattering scale with no volumetrics to scale.
    //
    // Same closed-struct rule as AtmosphereEnv: values, never the authoring component.
    struct SunLightFx
    {
        // UE's "Light Shafts" category, verbatim semantics: a bright-pass of the scene around the sun's
        // screen position, radially blurred toward it, added back before tonemapping.
        bool      LightShaftBloom    = false;
        float     BloomScale         = 0.2f;
        float     BloomThreshold     = 0.0f;
        float     BloomMaxBrightness = 100.0f;
        glm::vec3 BloomTint          = glm::vec3( 1.0f );

        // UE's Cloud Scattered Luminance Scale: scales this light's contribution scattered in the cloud
        // medium. Multiplies AtmosphereEnv::SunIrradiance, which the clouds alone consume.
        glm::vec3 CloudScatteredLuminanceScale = glm::vec3( 1.0f );

        // UE's "Affected By Atmosphere Transmittance": whether this sun's colour is multiplied by the
        // atmosphere's transmittance at ground level (physical model only — see the component).
        //
        // FALSE HERE, TRUE ON THE COMPONENT, and the difference is the point: this struct is reset to
        // its defaults whenever no directional light was chosen as the atmosphere sun, and "no sun was
        // chosen" must not silently redden a light that the sky is not driving. A chosen sun overwrites
        // this with its own authored value, which defaults to on as UE ships it.
        bool AffectedByAtmosphereTransmittance = false;
    };
} // namespace Desert::Graphic
