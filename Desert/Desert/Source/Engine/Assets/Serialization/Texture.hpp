#pragma once

#include <Common/Core/UUID.hpp>
#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Assets::Serialization
{
    struct TextureAssetData
    {
        Common::UUID Handle;
        std::string  SourcePath;
        std::string  CookedPath;

        uint32_t Width;
        uint32_t Height;
        uint32_t Channels;

        Desert::Core::Formats::ImageFormat Format;
    };
} // namespace Desert::Assets::Serialization