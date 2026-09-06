#pragma once

#include <cstddef>
#include <string>

namespace Desert::Editor
{
    // What one cook pass did — for the packager's summary line and for tests.
    struct CookStats
    {
        size_t ShadersCompiled = 0; // stages actually compiled this pass
        size_t ShadersCached   = 0; // stages already present under their key
        size_t FontsBaked      = 0;
        size_t FontsCached     = 0;
        size_t IconsBaked      = 0;
        size_t IconsCached     = 0;
        size_t Failures        = 0; // parse/compile/bake failures (each logged where it happened)
        // Artifacts that were produced and then did NOT reach the disk. Counted apart from Failures
        // because they mean something different: a compile failure is content that is already broken
        // (the runtime reports it too, and a project may legitimately ship one — the shader-error
        // fixture does), while an unwritten artifact is a cook that silently shipped nothing under a
        // key the runtime will ask for, which is П2 happening again one file at a time.
        size_t StoreFailures = 0;
    };

    // Pays, ONCE and at packaging time, every deterministic startup cost the runtime would otherwise
    // pay on the player's machine: compiles every stage of every pass of every shipped .shader,
    // bakes the default-size ASCII atlas of every shipped .ttf, and bakes the SDF layers of every
    // shipped .svg — each into its content-addressed home under the project's Cooked/ tree
    // (ShaderCache / FontCache / IconCache), through the exact key/path/store seams the runtime
    // reads back (ShaderSpirvCache, Text/FontCache, Vector/IconBake). The census tree
    // { COOKED_PATH, "Cooked" } then carries the artifacts into Content.dpak.
    //
    // Incremental by construction: an artifact already present under its key is not rebuilt.
    //
    // `spirvDebugInfo` is the TARGET runtime's profile (Core::SpirvDebugInfoForConfigName), not this
    // editor's: a Debug editor packaging a Release game must cook Release keys, or the shipped cache
    // is dead on arrival.
    CookStats CookContentCaches( bool spirvDebugInfo );
} // namespace Desert::Editor
