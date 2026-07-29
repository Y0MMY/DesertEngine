#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Common::Utils
{
    // The .dpak archive format (UE .pak-style): a file bundling shipped content. A build usually
    // ships SEVERAL paks (base + per-type chunks + patches) mounted as a stack — see VFS.
    //
    //   [ magic "DPK2" | u32 entryCount | u64 indexOffset ]              header (16 bytes)
    //   [ blob | blob | ... ]                                            raw file contents
    //   [ u32 pathLen | path utf8 | u64 offset | u64 size | u64 hash ]*  index (at indexOffset)
    //
    // hash = FNV-1a 64 of the entry's content: drives `PakTool diff` (patch-pak generation) and
    // post-download integrity checks. The reader still accepts v1 "DPK1" archives (no hash column;
    // EntryHash reports 0 for them). Paths are mount-root-relative, generic (forward-slash)
    // strings — e.g. "Assets/Scenes/Main.desce". No compression/encryption yet (a future version
    // bumps the magic again).

    // Content hash used by the pak index (FNV-1a 64) — public so tools/tests hash the same way.
    uint64_t PakContentHash( const void* data, size_t size );

    class PakWriter
    {
    public:
        // Begins a new archive (truncates). Check IsOpen() before adding.
        explicit PakWriter( const std::filesystem::path& pakPath );

        bool IsOpen() const;

        // Adds one file under the given mount-relative key ("Assets/x.desce"). Returns false on IO error.
        bool AddFile( const std::string& key, const std::filesystem::path& sourceFile );
        bool AddData( const std::string& key, const void* data, size_t size );

        // Writes the index + header. Returns entry count written (0 = failure/empty).
        size_t Finalize();

    private:
        struct Entry
        {
            std::string Key;
            uint64_t    Offset = 0;
            uint64_t    Size   = 0;
            uint64_t    Hash   = 0; // FNV-1a 64 of the content
        };

        std::filesystem::path m_Path;
        std::vector<Entry>    m_Entries;
        uint64_t              m_Cursor = 0;
        bool                  m_Ok     = false;
    };

    class PakReader
    {
    public:
        // Opens + parses the index. Check IsOpen().
        explicit PakReader( const std::filesystem::path& pakPath );

        bool   IsOpen() const;
        size_t EntryCount() const;

        bool Contains( const std::string& key ) const;
        std::optional<uint64_t> EntrySize( const std::string& key ) const;
        // Content hash from the index (0 for v1 archives that predate hashing).
        std::optional<uint64_t> EntryHash( const std::string& key ) const;

        // Reads one entry (opens its own stream — safe to call from any thread).
        std::optional<std::string> Read( const std::string& key ) const;

        // Keys under the given prefix ("Cooked/Meshes"); prefix "" = everything.
        std::vector<std::string> KeysWithPrefix( const std::string& prefix ) const;

    private:
        struct Span
        {
            uint64_t Offset = 0;
            uint64_t Size   = 0;
            uint64_t Hash   = 0; // 0 when the archive is v1 (pre-hash)
        };

        std::filesystem::path                 m_Path;
        std::unordered_map<std::string, Span> m_Index;
        bool                                  m_Ok = false;
    };
} // namespace Common::Utils
