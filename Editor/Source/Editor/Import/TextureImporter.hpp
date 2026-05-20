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

    private:
        std::unordered_map<std::string, Common::UUID> m_Cache;
    };
} // namespace Desert::Editor