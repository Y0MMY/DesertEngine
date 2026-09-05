#pragma once

#include <Common/Core/UUID.hpp>
#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Assets::Serialization
{
    struct TextureAssetData
    {
        Common::UUID Handle;

        // The source image's place inside the project, in AssetHandle::StableKeyForPath form
        // (`assets:Textures/T.png`) — never an absolute path, which is what this field used to carry and
        // what made every committed .tex loadable on exactly one machine. Resolved back through
        // AssetHandle::PathForStableKey at load time. A `CookedPath` field used to sit beside this one,
        // naming a `.dds` no code ever wrote or read; readers ignore it in old files.
        std::string SourcePath;

        uint32_t Width;
        uint32_t Height;
        uint32_t Channels;

        Desert::Core::Formats::ImageFormat Format;
    };
} // namespace Desert::Assets::Serialization