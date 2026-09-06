#pragma once

// Starting another process — the launcher's whole reason to exist, and until now its worst code.
//
// WHAT WAS WRONG. LaunchEditor spliced a SHELL string and handed it to std::system(), which is
// /bin/sh -c <string>. Measured on this machine against the real composition, with a recorder
// standing in for RunEditor.sh:
//
//     /tmp/My Game/My Game.deproj       -> arrives intact (a quoted space survives)
//     /tmp/Odd $HOME/Odd.deproj         -> the Editor received /tmp/Odd /Users/daniilsavcenko/...
//     /tmp/qu"ote/Q.deproj              -> sh: unexpected EOF; the Editor NEVER STARTED
//     /tmp/back`id -u`tick/B.deproj     -> `id -u` was EXECUTED, and its output landed in the path
//
// So the failure is not the space the design notes guessed at — it is `$`, `"` and the backtick,
// and the last of those is arbitrary command execution driven by a folder name. On top of that
// std::system's result was discarded and LaunchEditor returned true unconditionally: the quote
// case closed the hub while reporting success, which is the silent-substitution class this repo
// forbids.
//
// THE ANSWER, and it is the one the design already committed to for the HTTP client (D-http §4.5,
// requirement 1): an argv ARRAY through posix_spawn / CreateProcessW, with no shell anywhere in
// the chain. An argv element is bytes; there is no metacharacter to escape because nothing parses
// it. The same rule closes "Reveal in Finder", which had the identical splice.
//
// The composition is a PURE FUNCTION (BuildEditorLaunch) so that the launcher's half of D-launch
// — "the command it composes uses exactly the shared protocol constants" — is a test and not a
// promise. LaunchProtocol.hpp claims that test exists; before this file it did not.

#include <string>
#include <vector>

#include <DesertShared/ResultStr.hpp>

namespace Hub
{
    // What the OS will be asked to run. A value, not a command line: Arguments are argv[1...],
    // each one a verbatim byte string that no shell will re-read.
    struct LaunchCommand
    {
        std::string              Program;          // ABSOLUTE path of the executable
        std::vector<std::string> Arguments;        // argv[1..]; argv[0] is filled in by the spawn
        std::string              WorkingDirectory; // "" = inherit the hub's
    };

    // The Editor launch for one project. `engineRoot` is DESERT_ROOT, `config` one of
    // Common::Launch::kConfigDebug / kConfigRelease, `deprojPath` the descriptor to open.
    //
    // The two platforms reach the Editor differently ON PURPOSE, and the difference is the
    // MoltenVK environment, not taste:
    //
    //   * macOS goes through scripts/MacOS/RunEditor.sh, because that script is the ONE place that
    //     exports VK_ICD_FILENAMES / VK_LAYER_PATH / DYLD_FALLBACK_LIBRARY_PATH, without which the
    //     Editor dies before its first frame. A script is not a shell string: the kernel honours
    //     its shebang and our argv arrives as "$@", untouched.
    //   * Windows spawns build\Bin\<config>\Editor.exe directly, because RunEditor.bat does
    //     nothing a spawn cannot (a cd, an existence check) and reaching a .bat at all would mean
    //     going through cmd.exe — putting a shell back in the chain to run a shell-free launch.
    //
    // Pure: touches no disk and no environment, which is what lets the suite check it.
    [[nodiscard]] LaunchCommand BuildEditorLaunch( const std::string& engineRoot, const std::string& config,
                                                   const std::string& deprojPath );

    // Reveals a file or folder in the platform's file manager (Finder / Explorer). The tool is
    // resolved by ABSOLUTE path, never by name — the same rule D-http §4.5(2) set for curl: a
    // program found through PATH is a program an earlier entry can replace.
    [[nodiscard]] LaunchCommand BuildRevealCommand( const std::string& path );

    // Starts the command and does not wait for it. Returns the reason on failure — a launch that
    // could not happen must reach the caller, because the hub's next act is to close itself.
    [[nodiscard]] Common::BoolResultStr SpawnDetached( const LaunchCommand& command );
} // namespace Hub
