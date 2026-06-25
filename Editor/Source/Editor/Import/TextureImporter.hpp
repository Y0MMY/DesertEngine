#pragma once

#include <memory>
#include <filesystem>
#include <unordered_map>

#include <Common/Core/UUID.hpp>

namespace Desert::Editor
{
    class TextureImporter
    {
    public:
        Common::UUID Import( const std::filesystem::path& path );

        // The cooked metadata (.tex) path a given source texture is cooked into (Cooked/Textures/...).
        // Public so callers can CreateAsset<TextureAsset>() on it right after Import().
        static std::filesystem::path CookedMetaPath( const std::filesystem::path& source );

    private:
        std::unordered_map<std::string, Common::UUID> m_Cache;
    };
} // namespace Desert::Editor