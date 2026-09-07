#pragma once

#include <Common/Project/ProjectFormat.hpp>

#include <string>
#include <vector>

namespace Desert::Project
{
    // A Desert project = a folder with a .deproj JSON file describing it. ENGINE-level concept: both the
    // Editor and the Runtime player open one (`--project <path>`), which REMAPS every content path
    // (Common::Constants::Path) into the project folder. The Project Hub (Tools/ProjectHub) creates
    // projects and launches the apps — which is why the STRUCT lives in Common/Project/ProjectFormat.hpp:
    // the hub links Common and no engine code, and both sides must share one definition of the format.
    using ProjectFile = Common::Project::ProjectFile;

    class ProjectContext final
    {
    public:
        // Whether opening this project should touch `~/.desertengine/projects.json`.
        //
        // `No` exists for HEADLESS CAPTURE. An agent's `--shot` run opens a scratch project inside a
        // worktree that is reclaimed an hour later, and every one of those runs used to file itself
        // at the top of the developer's recent list: the live registry on this machine is mostly
        // paths into worktrees that no longer exist, and the launcher's whole "this entry cannot be
        // opened" state exists to survive them. A run that produces a PNG and exits is not a person
        // opening a project.
        enum class RecordInRecent
        {
            Yes,
            No
        };

        // Parses the .deproj, remaps the engine content paths to the project, creates missing standard
        // content folders and (unless told not to) moves the project to the top of the recent list.
        // Returns false when the file is missing/corrupt.
        static bool Open( const std::string& deprojPath, RecordInRecent record = RecordInRecent::Yes );

        // Persist the in-memory project back to its own .deproj. No-op (returns false) without a project.
        static bool Save();

        // Set the startup scene (path RELATIVE to the project directory, e.g. "Assets/Scenes/Main.desce")
        // and persist. Pass "" to clear it. Returns false without a project.
        static bool SetDefaultScene( const std::string& sceneRelPath );

        static bool HasProject();
        static const ProjectFile& Current();   // valid only when HasProject()
        static std::string        Directory(); // the folder the .deproj lives in
        static std::string        FilePath();  // the .deproj path itself

        // Absolute path of the project's default scene ("" when the project has none / no project).
        static std::string DefaultScenePath();

        // Recent projects (most recent first) from <config>/projects.json (shared with the Project
        // Hub). The whole registry, not a list of paths: each entry carries the LastOpened the
        // launcher draws its relative time from, and RegisterRecent has to write the entries back.
        static Common::Project::ProjectsRegistry RecentProjects();

        // ~/.desertengine (created on demand) — user-level config shared by the tools (projects.json,
        // the editor's editor.json).
        static std::string ConfigDirectory();

    private:
        static void RegisterRecent( const std::string& deprojPath );
    };
} // namespace Desert::Project
