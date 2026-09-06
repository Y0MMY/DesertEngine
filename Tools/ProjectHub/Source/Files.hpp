#pragma once

// The hub's file primitives. Two of them, and only two: read a whole text file, and replace one
// atomically. The launcher links no engine code (R1), so this is its own copy of the engine's
// Common::Utils::FileSystem — kept deliberately tiny, because the two files it guards
// (~/.desertengine/projects.json and <project>/<Name>.deproj) are shared with the Editor.

#include <filesystem>
#include <string>

#include <DesertShared/ResultStr.hpp>

namespace Hub
{
    // Reads a whole file. A missing or unreadable file is an ERROR with the reason, not an empty
    // string: "the registry is empty" and "the registry could not be read" used to look identical
    // to every caller, and one of them means the user's project list is still on disk.
    [[nodiscard]] Common::ResultStr<std::string> ReadTextFile( const std::filesystem::path& path );

    // Atomic (write-then-rename), for two reasons. First, the registry this writes is shared with
    // the Editor, and an in-place truncate means an interruption leaves a torn projects.json for
    // BOTH of them — the whole recent list gone over one crash. Second, the obvious name WriteFile
    // is a windows.h macro (WriteFileA); this one compiles on purpose, not by coincidence.
    //
    // Same steps as the engine's WriteContentToFileAtomic, for the same reasons: the temp lands
    // BESIDE the destination (rename is only atomic within one filesystem), the stream is checked
    // after close() (where a buffered failure finally surfaces), the temp name is FIXED so
    // concurrent writers race to a whole file instead of interleaving into a torn one, and a
    // failure at any step leaves the original untouched.
    //
    // Returns the reason on failure — the caller must be able to say WHICH step failed and that
    // the destination is unchanged, not merely that "something went wrong".
    [[nodiscard]] Common::BoolResultStr WriteTextFile( const std::filesystem::path& path,
                                                       const std::string&           content );
} // namespace Hub
