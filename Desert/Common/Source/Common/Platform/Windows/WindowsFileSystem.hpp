#pragma once

#include <Common/Utilities/FileSystem.hpp>

namespace Common::Utils
{
    class WindowsFileSystem
    {
    public:
        static std::filesystem::path OpenFileDialog( const char* filter );
        static std::filesystem::path OpenFolderDialog( const char* initialFolder );
        static std::filesystem::path SaveFileDialog( const char* filter );
    };
} // namespace Common::Utils