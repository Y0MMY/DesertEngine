#pragma once

// The ONE definition of the on-disk project formats, shared by every process that touches them:
//
//   * the Engine — Editor and Runtime open a `.deproj` through Engine/Project/ProjectContext and
//     maintain the recent-projects registry `<config>/projects.json`;
//   * the Project Hub (Tools/ProjectHub) — creates `.deproj` files, reads and writes the registry;
//   * the GamePackager — regenerates the `.deproj` it ships next to a packaged game.
//
// Before this header each of those carried its own copy of the format: the engine had the reflected
// structs, while the hub and the packager spliced JSON by hand and a comment asked everyone to "keep
// the field name in sync". A typo on the writing side produced a project the engine silently refused
// to open, and the hand-rolled writers could not escape a quote in a project name at all. Now the
// struct IS the format (rfl::json reflects the member names into the file), and both sides go
// through the same two functions — a producer and a consumer that cannot disagree because they are
// the same code. Tests/Engine/ProjectFormat pins the on-disk field names, so renaming a member here
// (which would orphan every .deproj already on disk) fails the suite instead of failing users.
//
// Lives in Common (not Engine/Project) because the hub links Common and no engine code; future
// formats the hub grows — project templates, downloadable collections — belong beside these structs
// for the same reason.

#include <Common/Core/Constants.hpp>
#include <Common/Core/ResultStr.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Common::Project
{
    // <project>/<Name>.deproj — the project descriptor. Member names are the file format.
    struct ProjectFile
    {
        std::string Name;
        std::string AssetsRoot   = "Assets";
        std::string DefaultScene = ""; // relative to the project directory; "" = no startup scene
    };

    // <config>/projects.json — the recent-projects registry (most recent first, .deproj paths).
    struct ProjectsRegistry
    {
        std::vector<std::string> Projects;
    };

    // One standard content folder of a project: its path relative to the assets root (what a creator
    // scaffolds on disk) tied to the Constants::Path global the engine reads it back through. Keeping
    // the two in one row is the point — the census used to exist as three independent literal lists
    // (the hub's, ProjectContext::Open's, and SetProjectRoot's), and only luck kept them equal.
    // Tests/Engine/ProjectFormat asserts the RELATION: after SetProjectRoot, *EnginePath must equal
    // assetsRoot / RelativePath for every row.
    struct ContentFolder
    {
        std::string_view             RelativePath; // under the project's assets root, e.g. "Meshes/"
        const std::filesystem::path* EnginePath;   // the Constants::Path global the engine reads
    };

    // The folders every project is created with and the engine re-creates on open. The Clouds/*
    // folders of Constants::Path are deliberately absent: they are made on demand by the bake panels
    // that produce their content, and scaffolding them empty would advertise features a project may
    // never use.
    inline const std::array<ContentFolder, 7> StandardContentFolders = { {
         { "Meshes/", &Common::Constants::Path::MESH_PATH },
         { "Materials/", &Common::Constants::Path::MATERIAL_PATH },
         { "Textures/", &Common::Constants::Path::TEXTUREDIR_PATH },
         { "Scenes/", &Common::Constants::Path::SCENE_PATH },
         { "Prefabs/", &Common::Constants::Path::PREFAB_PATH },
         { "Scripts/", &Common::Constants::Path::SCRIPT_PATH },
         { "Collections/", &Common::Constants::Path::COLLECTIONS_PATH },
    } };

    // Serialization — implemented once, over rfl::json, in ProjectFormat.cpp. Readers return the
    // parse error VERBATIM so the caller can say why a file was refused instead of refusing quietly.
    [[nodiscard]] Common::ResultStr<ProjectFile>      ReadProjectFile( const std::string& json );
    [[nodiscard]] std::string                         WriteProjectFile( const ProjectFile& file );
    [[nodiscard]] Common::ResultStr<ProjectsRegistry> ReadProjectsRegistry( const std::string& json );
    [[nodiscard]] std::string                         WriteProjectsRegistry( const ProjectsRegistry& registry );
} // namespace Common::Project
