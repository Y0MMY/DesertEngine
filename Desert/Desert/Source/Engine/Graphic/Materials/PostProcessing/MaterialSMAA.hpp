#pragma once

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    // SMAA 1x is a 3-pass technique; each pass has its own shader and input bindings, so it gets its own
    // tiny material (mirrors MaterialFXAA). All run on the LDR tonemapped image.

    // Pass 1: luma edge detection (input = scene color).
    class MaterialSMAAEdges final : public Material
    {
    public:
        MaterialSMAAEdges();
        void Bind( const std::shared_ptr<Image2D>& color );

    private:
        Texture2DProperty* m_Color = nullptr;
    };

    // Pass 2: blending-weight calculation (inputs = edges + AreaTex + SearchTex LUTs).
    class MaterialSMAAWeights final : public Material
    {
    public:
        MaterialSMAAWeights();
        void Bind( Image2D* edges, Image2D* area, Image2D* search );

    private:
        Texture2DProperty* m_Edges  = nullptr;
        Texture2DProperty* m_Area   = nullptr;
        Texture2DProperty* m_Search = nullptr;
    };

    // Pass 3: neighborhood blending (inputs = scene color + blend weights).
    class MaterialSMAABlend final : public Material
    {
    public:
        MaterialSMAABlend();
        void Bind( const std::shared_ptr<Image2D>& color, Image2D* weights, Image2D* edges, Image2D* area );

    private:
        Texture2DProperty* m_Color = nullptr;
        Texture2DProperty* m_Blend = nullptr;
        Texture2DProperty* m_Edges = nullptr; // DIAGNOSTIC only
        Texture2DProperty* m_Area  = nullptr; // DIAGNOSTIC only
    };
} // namespace Desert::Graphic
