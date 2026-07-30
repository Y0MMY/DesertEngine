#pragma once

#include <Common/Utilities/FileSystem.hpp>

namespace Common::Utils
{
    class MacOSFileSystem
    {
    public:
        // filter is the Windows-style "Description\0*.ext\0" string; the native
        // NSOpenPanel does its own type filtering UI, so it is ignored here.
        static std::filesystem::path OpenFileDialog( const char* filter );

        // Directory chooser (NSOpenPanel with canChooseDirectories). initialFolder is used as the starting
        // location when non-empty; the filter concept does not apply to folders.
        static std::filesystem::path OpenFolderDialog( const char* initialFolder );

        // Save-as chooser (NSSavePanel). filter is ignored (same reason as OpenFileDialog).
        static std::filesystem::path SaveFileDialog( const char* filter );
    };
} // namespace Common::Utils
