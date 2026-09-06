#pragma once

// WHERE a compiled SPIR-V artifact lives and HOW it is read back — one definition, three consumers:
//
//   * Core::ShaderCompiler stores every fresh compile here and asks here before compiling;
//   * the game packager (Editor/Packaging) cooks every shipped shader into this exact location, so
//     the census tree { COOKED_PATH, "Cooked" } carries the artifacts into Content.dpak;
//   * a packaged game reads them back OUT of that archive: the load is VFS-aware (loose file first,
//     then the mounted pak), because a player's install has no loose Cooked/ tree at all.
//
// The load being VFS-aware is the entire point of this file existing. It used to be a raw
// std::ifstream inside ShaderCompiler.cpp, which meant the packager shipped a warm cache the
// runtime could not see — every startup of a packaged game recompiled every shader it already had.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Desert::Core
{
    // Cooked/ShaderCache/<key as 16 hex digits>.spv under the CURRENT project's cooked tree (the
    // live COOKED_PATH constant, so a project remap is followed automatically).
    std::filesystem::path SpirvCachePathForKey( uint64_t key );

    // Loose file first (dev override), then the mounted .dpak. nullopt on miss or a torn artifact
    // (size not a whole number of SPIR-V words) — a corrupt cache entry is simply recompiled.
    std::optional<std::vector<uint32_t>> TryLoadCachedSpirv( uint64_t key );

    // Whether the artifact actually landed on disk. The RUNTIME may ignore this — a read-only
    // install (inside an .app bundle) simply keeps no cache, which is the documented contract. The
    // PACKAGER may not: a cook that could not write is a cook that ships nothing under that key, and
    // every player then pays the compile it was supposed to have been spared. Silence there would
    // reintroduce П2 one artifact at a time, so the return value exists to be checked.
    bool StoreCachedSpirv( uint64_t key, const std::vector<uint32_t>& spirv );
} // namespace Desert::Core
