#pragma once

// ONE MISSING FIELD USED TO DELETE THE WHOLE COMPONENT, AND NOTHING SAID SO.
//
// THE DEFECT, MEASURED. Eight components in ComponentRegistry.cpp are mapped through a reflect-cpp
// mirror struct — Script, StaticMesh, SkinnedMesh, InstancedStaticMesh, Material, UIAnim, Text and
// Animation. Reading one used to be `rfl::json::read<T>( ... )` with no processor, and reflect-cpp
// treats a MISSING field as an error even when the struct declares a default for it:
//
//     read<AnimationComponentSer>(R"({"CurrentClip":"Run","Playing":true,"Loop":true,"PlaybackSpeed":2})")
//       -> error: Found 2 errors: 1) Field named 'EnableRootMotion' not found.
//                                 2) Field named 'GraphJson' not found.
//
// The call sites turned that into `if ( !parsed.has_value() ) return;` — eight of them, none with a
// log line. So a block that was short of ONE field did not lose that field: the entity lost the whole
// component, silently. An animated character stopped being animated, a text element stopped having
// text, and the log had nothing in it.
//
// WHY THAT IS NOT HYPOTHETICAL. ForeignKeys.hpp states the rule this project works by — "a field
// ADDED needs no version bump, a missing key already defaults" — and builds the whole preservation
// argument on it. For these eight components the rule was FALSE: adding one field to a mirror struct
// silently voided every scene already written, and this repository routinely has ten worktrees alive
// at different commits, each able to open the editor and save. Two files stating opposite things
// about the same load is the shape §4 of the contract calls a relation defect; the comment was right
// and the code was wrong, so the code moved.
//
// THE FIX IS THE PROCESSOR, AND THE OTHER HALF IS THE LOG. `rfl::DefaultIfMissing` makes an absent
// field take the struct's own default, which is what every caller already believed. A payload that is
// genuinely malformed — a string where a float belongs — still refuses, and now it refuses OUT LOUD,
// with the component's key and reflect-cpp's own reason. An empty successful answer is a silent wrong
// answer (contract §1.4), and dropping a component with no line in the log was exactly that.
//
// The direction that was already safe stays safe and is pinned by a test: an UNKNOWN extra field does
// not refuse, which is what lets an older build open a newer build's scene.
//
// PURE, AND A HEADER FOR THAT REASON. ComponentRegistry.cpp links the AssetManager and through it the
// renderer, so nothing defined inside it can be exercised by a suite. These two functions can, and
// Desert/Tests/Engine/GenericBlockRead does.

#include <Common/Core/Logger.hpp>

#include <rflcpp/rfl/Generic.hpp>
#include <rflcpp/rfl/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace Desert::Core::Serialize
{
    // Bridges a typed serialization struct to the generic JSON tree a `.desce` carries, reusing
    // reflect-cpp's own serialization for the verbose asset-bearing payloads (mesh vertices, material
    // path lists). @p key is the component's registry key and exists only so a refusal can name itself.
    template <class T>
    rfl::Generic WriteBlock( const T& value, std::string_view key )
    {
        auto generic = rfl::json::read<rfl::Generic>( rfl::json::write( value ) );
        if ( generic.has_value() )
            return generic.value();

        // Unreachable unless reflect-cpp cannot re-read what it has just written, which would be a
        // defect in the mirror struct itself. It used to fall back to `{}` without a word, and an
        // empty block is indistinguishable from a component with nothing set in it.
        LOG_ERROR( "[Scene] component '{0}' could not be written: {1}. An EMPTY block was stored under "
                   "its key instead.",
                   std::string( key ), generic.error().what() );
        return rfl::Generic( rfl::Generic::Object{} );
    }

    // The other direction. `DefaultIfMissing` is the whole point — see the header comment.
    template <class T>
    std::optional<T> ReadBlock( const rfl::Generic& generic, std::string_view key )
    {
        auto parsed = rfl::json::read<T, rfl::DefaultIfMissing>( rfl::json::write( generic ) );
        if ( parsed.has_value() )
            return parsed.value();

        LOG_ERROR( "[Scene] component '{0}' could not be read: {1}. The component was DROPPED from the "
                   "entity — whatever it held is not in the scene that just opened.",
                   std::string( key ), parsed.error().what() );
        return std::nullopt;
    }
} // namespace Desert::Core::Serialize
