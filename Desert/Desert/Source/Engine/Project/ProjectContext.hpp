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
    //
    // WHAT BELONGS IN A .deproj, in one sentence (К1): WHAT THE PRODUCT IS, FOR EVERYBODY — the few facts
    // every process that opens this project must agree on before any level exists (its identity, where its
    // content lives, which level boots, the format's own version). What does NOT belong: anything that
    // varies from level to level (that is the .desce), anything that varies from machine to machine (that
    // is ~/.desertengine/editor.json), and any field whose value can be derived — Common/Core/Constants.hpp
    // already refused fourteen folder-name fields on exactly that ground, and the refusal is the model.
    //
    // The struct's field list is the format and is defined once, in the desert-shared submodule; the
    // three-question procedure that decides where a NEW field goes, and the census that goes red when one
    // lands in the wrong file, are in Desert/Tests/Engine/ConfigOwnership. That suite also holds a tripwire
    // on the .deproj this repository tracks: `EngineVersion` is a fact about a MACHINE (Save() stamps
    // Common::Version::Full(), commit hash and `.dirty` included) written into a file the whole team shares,
    // and it is registered there as debt with the task that owns moving it.
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
        //
        // A REFUSAL AND AN EMPTY LIST ARE DIFFERENT ANSWERS (DC §1.4), and this used to return the
        // same value for both. A machine with no registry yet has no projects; a registry that
        // exists and does not parse has all of them, and the caller below WRITES what it gets back
        // — so collapsing the two turned one unparseable byte into "every project you ever opened
        // is gone". Absent file = an empty registry, successfully. Anything else = the reason.
        //
        // The config directory is an ARGUMENT, on the same terms as EngineRegistration's: it is what
        // lets a suite point this at a temp folder instead of at the developer's own ~/.desertengine,
        // and without it the two-writer protocol below would be reachable by no test at all.
        [[nodiscard]] static Common::ResultStr<Common::Project::ProjectsRegistry>
        RecentProjects( const std::string& configDirectory );

        // Files `deprojPath` at the top of the registry in `configDirectory` — the ENGINE's half of a
        // file two programs write (Tools/ProjectHub is the other). Public because it is one half of a
        // cross-process protocol and a test has to be able to play the other half.
        //
        // READ-MODIFY-WRITE: the registry is re-read here, immediately before the write, never held
        // from earlier. A registry that cannot be read is NOT overwritten — the reason is logged and
        // the file is left exactly as it is.
        static void RegisterRecent( const std::string& configDirectory, const std::string& deprojPath );

        // ~/.desertengine (created on demand) — user-level config shared by the tools (projects.json,
        // the editor's editor.json).
        static std::string ConfigDirectory();
    };
} // namespace Desert::Project
