#pragma once

#include <filesystem>
#include "ImportResult.hpp"

namespace Desert::Editor
{
    class IAssetImporter
    {
    public:
        virtual ~IAssetImporter() = default;

        virtual ImportResult Import( const std::filesystem::path& path ) = 0;
    };
} // namespace Desert::Editor