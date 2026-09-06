#pragma once

// Everything the hub knows about projects that is NOT drawing: what a registry entry resolves to,
// how the recent list is kept, what a legal project name is, and how a project is created. It
// lives outside Main.cpp so the suite can run it — the launcher's screens need a window, its
// contracts do not.

#include <string>
#include <utility>
#include <vector>

#include <DesertShared/ProjectFormat.hpp>
#include <DesertShared/ResultStr.hpp>

namespace Hub
{
    // One line of ~/.desertengine/projects.json, resolved against the disk.
    //
    // The registry stores paths and nothing else, so a card used to be drawn from the path's STEM
    // with no check that anything was there: a project deleted, renamed or living in a worktree
    // that has since been reclaimed looked exactly like a working one, and clicking Open led
    // nowhere. Both halves are fixed here — the displayed name comes from the descriptor the
    // engine will actually read, and an entry that cannot be opened says so before it is clicked.
    struct ProjectEntry
    {
        std::string Path; // the .deproj path, verbatim as the registry stores it
        std::string Name; // ProjectFile::Name when the descriptor reads; the file stem otherwise
        // "" when this entry can be opened. Otherwise the VERBATIM reason it cannot — missing
        // file, unreadable file, or the parser's own words about a corrupt one. A corrupt .deproj
        // is as unopenable as a deleted one and the engine refuses it into a log nobody sees, so
        // both states are the same state here.
        std::string Trouble;

        [[nodiscard]] bool IsOpenable() const
        {
            return Trouble.empty();
        }
    };

    [[nodiscard]] ProjectEntry              ResolveProjectEntry( const std::string& deprojPath );
    [[nodiscard]] std::vector<ProjectEntry> ResolveProjectEntries( const std::vector<std::string>& paths );

    // Moves `deprojPath` to the front of the recent list, keeping it unique.
    //
    // There is no cap. There used to be one — ten entries, silently dropping the eleventh — and it
    // is gone for a reason that had to arrive first: with missing entries invisible, a list that
    // grew forever would have filled with dead paths nobody could see to remove. Now every entry
    // states whether it is openable and carries a Remove button, so the list is curated instead of
    // truncated. The Editor's ProjectContext::RegisterRecent is the OTHER writer of this file and
    // lost its cap in the same change; until the recent-list policy moves into desert-shared with
    // the {Path, LastOpened} registry (L2 §10.4, stage E1), those two copies must agree — an
    // uncapped hub next to a capped engine would silently lose the list on the next project open.
    void PromoteRecent( std::vector<std::string>& recent, const std::string& deprojPath );

    // "" when the name can be a project folder; otherwise the user-facing reason it cannot.
    // Without this a name of "../../etc" scaffolded a tree outside the chosen location and wrote a
    // descriptor whose own folder was not where the hub said it was.
    [[nodiscard]] std::string ValidateProjectName( const std::string& name );

    // A starter project template: extra folders on top of the standard census, whether the .deproj
    // points at a default scene (the Editor authors the file itself on first open), and any starter
    // files to drop in.
    //
    // STILL HARDCODED IN C++, and that is L2's to move, not this task's: L2 §3 places templates in
    // <engine-root>/Templates/<Id>/ with a template.json manifest and a byte-copied Payload/, which
    // is the only way a third template arrives without a rebuild. What breaks TODAY because they
    // live here: a template cannot carry content (the hub links no engine code, so it cannot author
    // a .desce or a material), which is why "3D Sandbox" ships a commented-out Lua file instead of
    // a scene; and the three of them are compiled into every binary, so the set is a property of
    // the launcher build rather than of the engine install it launches.
    struct ProjectTemplate
    {
        const char*                                      Id;
        const char*                                      Title;
        const char*                                      Description;
        std::vector<const char*>                         ExtraFolders;    // under the assets root
        bool                                             SetDefaultScene; // write DefaultScene into the .deproj
        std::vector<std::pair<std::string, std::string>> Files;           // project-relative path -> contents
    };

    [[nodiscard]] const std::vector<ProjectTemplate>& Templates();

    // Scaffolds a project and returns the path of the .deproj it wrote.
    //
    // Every step is checked and every failure is named. It also CLEANS UP after itself: a create
    // that fails half way used to leave a folder tree the engine would never open sitting in the
    // user's chosen location, and the next attempt with the same name then refused because "the
    // folder is not empty". The cleanup only ever removes a directory this call created.
    [[nodiscard]] Common::ResultStr<std::string> CreateProject( const std::string&     parentDirectory,
                                                                const std::string&     name,
                                                                const ProjectTemplate& projectTemplate );
} // namespace Hub
