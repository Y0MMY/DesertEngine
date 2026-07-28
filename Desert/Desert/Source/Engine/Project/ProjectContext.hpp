#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Desert::Project
{
    // A Desert project = a folder with a .deproj JSON file describing it. ENGINE-level concept: both the
    // Editor and the Runtime player open one (`--project <path>`), which REMAPS every content path
    // (Common::Constants::Path) into the project folder. The Project Hub (Tools/ProjectHub) creates
    // projects and launches the apps.
    struct ProjectFile
    {
        std::string Name;
        std::string AssetsRoot   = "Assets";
        std::string DefaultScene = ""; // relative to the project directory
    };

    class ProjectContext final
    {
    public:
        // Parses the .deproj, remaps the engine content paths to the project, creates missing standard
        // content folders and moves the project to the top of the recent list. Returns false when the
        // file is missing/corrupt.
        static bool Open( const std::string& deprojPath );

        static bool HasProject();
        static const ProjectFile& Current();   // valid only when HasProject()
        static std::string        Directory(); // the folder the .deproj lives in
        static std::string        FilePath();  // the .deproj path itself

        // Absolute path of the project's default scene ("" when the project has none / no project).
        static std::string DefaultScenePath();

        // Recent projects (most recent first) from <config>/projects.json (shared with the Project Hub).
        static std::vector<std::string> RecentProjects();

        // ~/.desertengine (created on demand) — user-level config shared by the tools (projects.json,
        // the editor's editor.json).
        static std::string ConfigDirectory();

    private:
        static void RegisterRecent( const std::string& deprojPath );
    };
} // namespace Desert::Project
