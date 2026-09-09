#pragma once

// Reading the REPOSITORY from inside this suite.
//
// Both translation units of this test binary assert relations against files that are checked in — the
// Volume half reads the shipped shader tree, the compiler half reads the `.dgraph` corpus the project
// actually ships. They used to reach the tree through a private copy of these two helpers each, which is
// the shape this project keeps paying for: two definitions of one thing, drifting the day one of them is
// taught something (a deeper search, a different anchor file). One definition, in a header both include.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace Desert::Tests::ShaderGraph
{
    /// The repository root, found by walking up until an anchor that only this repository has.
    ///
    /// The anchor is a SOURCE file rather than a directory name: a suite is run from wherever the sweep
    /// happens to stand (`build/Bin/Tests/Debug`, the repo root, a worktree), and "does a folder called
    /// Desert exist" is true in several of those places for the wrong reason.
    inline std::filesystem::path RepoRoot()
    {
        std::filesystem::path prefix = ".";
        for ( int up = 0; up < 6; ++up )
        {
            if ( std::filesystem::exists( prefix / "Desert/Desert/Source/Engine/Core/SceneSettings.hpp" ) )
                return prefix;
            prefix /= "..";
        }
        return {};
    }

    /// Whole file as bytes, or empty when it is not there. Every caller asserts non-empty for itself,
    /// because "the file moved" and "the file no longer says this" are different findings and a helper
    /// that failed for the caller would blur them.
    inline std::string ReadAll( const std::filesystem::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
} // namespace Desert::Tests::ShaderGraph
