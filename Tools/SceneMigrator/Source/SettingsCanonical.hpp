#pragma once

// THE ONE PART OF THIS TOOL THAT NEEDS THE ENGINE'S REFLECTION TABLE, AND WHY IT IS ITS OWN FILE.
//
// Every step in SceneMigration.hpp is a pure function over the parsed tree and nothing else — which is
// what lets sixteen suites compile that one translation unit and test a step each without linking an
// engine. Canonicalisation cannot be that: "canonical" means "the bytes the engine's saver writes", so
// it has to enumerate the same fields through the same table and the same serializer, and a hand-written
// field list here would be a second statement of the format that could silently fork.
//
// Putting it beside the steps would have dragged Reflection.gen.cpp into all sixteen of those suites —
// three files and a full engine header sweep each, for a symbol fifteen of them never call. Hence the
// split: SceneMigration.cpp stays registry-free, and the two things that genuinely need the table (this
// file and the tool's main) carry it.

#include <optional>

#include <rflcpp/rfl/Generic.hpp>

namespace Desert::Migration
{
    // What CanonicaliseSettings did to one file.
    struct SettingsCanonicalisationReport
    {
        bool BlockCreated   = false; // the scene stated no Settings block at all
        int  KeysAdded      = 0;     // fields this build knows that the file did not state
        int  ValuesRestated = 0;     // fields whose TEXT changed without their value changing (see below)
        bool Refused        = false; // the reflection table was not available; the block is untouched
    };

    // Rewrites the Settings block into exactly the bytes the ENGINE'S SAVER would write for the values
    // the file states. Part of raising a scene from v13 to v14.
    //
    // WHY A CONVERSION IS NEEDED FOR SOMETHING THAT CHANGES NO VALUE. K11's relation is "a file read and
    // written back with no change is byte-identical to the source", and until this ran it was false for
    // every scene in the repository for two reasons that have nothing to do with foreign keys:
    //
    //   * a file states only the fields that existed when it was last saved — 26 of 51 in the oldest —
    //     and the saver writes all of them, so the first save of any old scene ADDS keys;
    //   * a float field hand-edited to `0.26` cannot survive a narrowing to `float` and back, and comes
    //     out as `0.2599999904632568`. The VALUE is identical (it is the same float either way); the
    //     TEXT is not, and byte-identity is a claim about text.
    //
    // Both are "the writer enumerates its registry and the file says something else", which is the same
    // disagreement K11 is about — so both are settled the same way and in the same place: in the FILES,
    // once, by a migration (§4.3, §4.5), rather than by every save of every build for ever.
    //
    // ASSET HANDLES ARE KEPT VERBATIM. A reflected AssetHandle is written as a PATH when the saver has an
    // asset resolver and as a raw integer when it does not; this tool has no AssetManager and must not
    // invent one, so any field of FieldType::AssetHandle keeps exactly the text the file already carried.
    // Without that, canonicalising the fourteen scenes that state `"SplashSprite": ""` would replace the
    // path form with a `0` and quietly change the format of a field.
    //
    // PURE apart from the process-wide reflection table it reads, which is const after static init. No
    // GPU, no filesystem, no scene.
    //
    // Idempotent: a block that is already canonical is left byte-identical and reports zero.
    SettingsCanonicalisationReport CanonicaliseSettings( std::optional<rfl::Generic>& settings );
} // namespace Desert::Migration
