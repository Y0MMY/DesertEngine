#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Common::Utils
{
    // Virtual file system: serves CONTENT reads from a mounted .dpak archive when the file is not on
    // disk. Wiring model (UE-style shipping):
    //
    //   - dev/editor: nothing mounted -> every read is a plain disk read (zero behavior change);
    //   - packaged game: the Runtime mounts <package>/Content.dpak (plus any chunk/patch paks);
    //     FileSystem::ReadFileContent/Exists/... try DISK FIRST (loose-file override for
    //     debugging), then fall back through the mount stack.
    //
    // MOUNT STACK: MountPak may be called repeatedly; LATER mounts override earlier ones for the
    // same key. That is the update mechanism — ship base.dpak (+ per-type chunks), then distribute
    // small patch paks containing only the changed files and mount them last.
    //
    // Lookup: an incoming path (absolute or cwd-relative) is normalized and made relative to each
    // pak's MOUNT ROOT (the directory the .dpak sits in); that relative generic string is the
    // archive key — e.g. "/pkg/Assets/S.desce" with a pak at "/pkg/Content.dpak" -> "Assets/S.desce".
    class VFS
    {
    public:
        // Mounts one archive on top of the stack. Returns false when missing/corrupt.
        static bool MountPak( const std::filesystem::path& pakFile );
        static bool IsMounted();
        static void Unmount();

        static bool                        Exists( const std::filesystem::path& path );
        static std::optional<std::string>  ReadFile( const std::filesystem::path& path );
        static std::optional<uint64_t>     FileSize( const std::filesystem::path& path );

        // Files under a directory prefix, returned as FULL paths (mountRoot / key) — callers treat them
        // exactly like disk paths; later reads resolve back through the VFS.
        static std::vector<std::filesystem::path> ListFiles( const std::filesystem::path& directory );
    };
} // namespace Common::Utils
