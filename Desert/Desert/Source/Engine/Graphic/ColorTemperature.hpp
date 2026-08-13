#pragma once

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    // Blackbody colour temperature to LINEAR RGB — Unreal's own conversion, verbatim semantics
    // (FLinearColor::MakeFromColorTemperature, Color.cpp): Krystek's 1985 Planckian-locus approximation
    // in CIE 1960 UCS, through xyY (Y = 1) and XYZ, into linear BT.709, clamped non-negative.
    //
    // It replaced Tanner Helland's curve-fit, which approximates GAMMA-encoded sRGB bytes — every colour
    // field in this engine is documented LINEAR, so feeding it Helland's values baked a hidden gamma into
    // the light the moment the slider moved. Note the result is NOT normalised to white at 6500 K
    // (D65 lands at ~(1.0000, 0.9445, 0.9853)); a caller that wants hue-only (the editor's Kelvin
    // slider, where Intensity owns brightness) divides by the peak itself.
    inline glm::vec3 ColorFromTemperature( float kelvin )
    {
        const float t = glm::clamp( kelvin, 1000.0f, 15000.0f );

        // Krystek 1985: u(T), v(T) on the Planckian locus in CIE 1960 UCS.
        const float u = ( 0.860117757f + 1.54118254e-4f * t + 1.28641212e-7f * t * t ) /
                        ( 1.0f + 8.42420235e-4f * t + 7.08145163e-7f * t * t );
        const float v = ( 0.317398726f + 4.22806245e-5f * t + 4.20481691e-8f * t * t ) /
                        ( 1.0f - 2.89741816e-5f * t + 1.61456053e-7f * t * t );

        // CIE 1960 -> CIE xy chromaticity, then xyY with Y = 1 -> XYZ.
        const float d = 2.0f * u - 8.0f * v + 4.0f;
        const float x = 3.0f * u / d;
        const float y = 2.0f * v / d;
        const float z = 1.0f - x - y;

        const float X = x / y;
        const float Y = 1.0f;
        const float Z = z / y;

        // XYZ -> linear BT.709.
        const float r = 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
        const float g = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
        const float b = 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

        return glm::vec3( glm::max( r, 0.0f ), glm::max( g, 0.0f ), glm::max( b, 0.0f ) );
    }
} // namespace Desert::Graphic
