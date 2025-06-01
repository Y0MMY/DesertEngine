#pragma once

#include <Engine/Graphic/UICacheTexture.hpp>

namespace Desert::ImGui
{
    class UICacheTextureImGui : public Graphic::UICacheTexture
    {
    public:
        virtual const void* AddTextureCache( const std::shared_ptr<Graphic::Image2D>& image ) override;
    };
} // namespace Desert::ImGui