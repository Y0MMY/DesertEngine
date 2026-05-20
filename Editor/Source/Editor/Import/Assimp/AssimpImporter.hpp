#pragma once

#include "../IAssetImporter.hpp"

namespace Desert::Editor
{
    class AssimpImporter : public IAssetImporter
    {
    public:
        ImportResult Import( const std::filesystem::path& path, ImportManager& manager ) override;
    };
} // namespace Desert::Editor