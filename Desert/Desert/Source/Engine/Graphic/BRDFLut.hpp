#pragma once

#include <cstdint>
#include <vector>

namespace Desert::Graphic
{
    // CPU generation of the split-sum BRDF integration LUT (Karis, "Real Shading in UE4"):
    // x = NdotV, y = roughness, RG = (scale, bias) applied to F0 in the IBL specular term.
    // Generated once at renderer init — the engine no longer ships/loads a BRDF_LUT texture file
    // (the old Resources/.../BRDF_LUT.tga dependency was missing anyway and IBL fell back to a
    // white dummy). RGBA32F layout, BA unused (0,1). Multithreaded; ~tens of ms in Release.
    std::vector<float> GenerateBRDFLutRGBA32F( uint32_t size, uint32_t samples );
} // namespace Desert::Graphic
