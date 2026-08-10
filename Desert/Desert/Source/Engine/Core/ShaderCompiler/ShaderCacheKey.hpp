#pragma once

// What a compiled shader is MADE OF — the include closure of a stage — and the content hash derived
// from it.
//
// One definition, two consumers, and that is the whole point of the file existing:
//
//   * Core::ShaderCompiler keys its SPIR-V disk cache on this hash. A key that does not cover an
//     included file is the worst kind of failure there is: the machine that has the stale artifact
//     renders differently from the machine that does not, and neither reports anything.
//   * Runtime::AssetHotReload watches these files for edits. A `.glslh` is not an asset and has no
//     mtime anybody was tracking, so editing one used to change nothing until the next restart — the
//     same staleness as an under-specified cache key, arriving through the other door.
//
// Both questions are "which files does this stage's SPIR-V depend on", so both are answered here.

#include <Engine/Core/Formats/Shader.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Desert::Core
{
    /**
     * Every file the assembled stage @p source pulls in, transitively, in the order first seen and
     * without duplicates.
     *
     * Mirrors ShaderIncluder's resolution exactly: `#include "x"` is relative to the including file,
     * `#include <x>` is relative to the engine shader root. A path that does not resolve is left OUT
     * rather than guessed at — a missing include is a compile error the compiler will report with a
     * better message than this walk could.
     *
     * @param requestingFile the file @p source came from; the anchor for quoted includes.
     */
    std::vector<std::filesystem::path> CollectShaderIncludes( const std::string&           source,
                                                              const std::filesystem::path& requestingFile );

    /**
     * The SPIR-V disk-cache key: stage + compile-options fingerprint + the assembled source + the
     * content of every file CollectShaderIncludes finds.
     *
     * Content-addressed on purpose — no mtimes, so a checkout that restores an older file is a
     * different key rather than a same-key-newer-timestamp, and two machines with the same tree agree.
     */
    uint64_t ComputeShaderCacheKey( Formats::ShaderStage stage, const std::string& source,
                                    const std::filesystem::path& requestingFile );
} // namespace Desert::Core
