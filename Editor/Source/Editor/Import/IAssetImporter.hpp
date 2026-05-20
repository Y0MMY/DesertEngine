#pragma once

#include <filesystem>
#include "ImportResult.hpp"

namespace Desert::Editor
{
    class ImportManager;
    class IAssetImporter
    {
    public:
        virtual ~IAssetImporter() = default;

        virtual ImportResult Import( const std::filesystem::path& path, ImportManager& manager ) = 0;
    };
} // namespace Desert::Editor