#pragma once

// Everything the hub knows about projects that is NOT drawing: what a registry entry resolves to,
// what a tile says about it, how templates are found on disk, what a legal project name is, and how
// a project is created. It lives outside Main.cpp so the suite can run it — the launcher's screens
// need a window, its contracts do not.

#include <string>
#include <vector>

#include <DesertShared/ProjectFormat.hpp>
#include <DesertShared/ResultStr.hpp>

namespace Hub
{
    // One line of ~/.desertengine/projects.json, resolved against the disk.
    //
    // The registry stores paths and a time, so a card used to be drawn from the path's STEM with no
    // check that anything was there: a project deleted, renamed or living in a worktree that has
    // since been reclaimed looked exactly like a working one, and clicking Open led nowhere. Both
    // halves are fixed here — the displayed name comes from the descriptor the engine will actually
    // read, and an entry that cannot be opened says so before it is clicked.
    struct ProjectEntry
    {
        std::string Path; // the .deproj path, verbatim as the registry stores it
        std::string Name; // ProjectFile::Name when the descriptor reads; the file stem otherwise
        std::string Description; // ProjectFile::Description; "" = none
        // "" when this entry can be opened. Otherwise the VERBATIM reason it cannot — missing
        // file, unreadable file, or the parser's own words about a corrupt one. A corrupt .deproj
        // is as unopenable as a deleted one and the engine refuses it into a log nobody sees, so
        // both states are the same state here.
        std::string Trouble;
        long long   LastOpened = 0; // Unix seconds UTC from the registry; 0 = never recorded
        // `<project>/.thumbnail.png` when the Editor has written one; "" otherwise. Resolved
        // against the disk, never assumed: a tile whose picture file does not exist draws its
        // placeholder rather than a broken-image frame.
        std::string ThumbnailPath;

        [[nodiscard]] bool IsOpenable() const
        {
            return Trouble.empty();
        }
    };

    [[nodiscard]] ProjectEntry ResolveProjectEntry( const std::string& deprojPath );
    [[nodiscard]] ProjectEntry ResolveProjectEntry( const Common::Project::ProjectRecord& record );
    [[nodiscard]] std::vector<ProjectEntry>
    ResolveProjectEntries( const Common::Project::ProjectsRegistry& registry );

    // `<project>/.thumbnail.png` if the file is there, "" if it is not. Written by the EDITOR on
    // save and on a clean exit; the launcher only ever reads it.
    [[nodiscard]] std::string ProjectThumbnailPath( const std::string& deprojPath );

    // "2 hours ago", "Yesterday", "Sep 1", "Aug 30" — and "" for 0, which is what every entry
    // migrated from the flat registry carries. Empty is a real answer: a tile with no time shows no
    // time rather than "Jan 1 1970" or a guess.
    [[nodiscard]] std::string RelativeTime( long long lastOpenedUnix, long long nowUnix );

    // Columns in the project grid — L2 §6.1 verbatim: floor(content width / 250), minimum 2. This
    // IS the resize behaviour, which is why it is a function with a test rather than a line inside
    // the draw loop.
    [[nodiscard]] int GridColumns( float contentWidth );

    // "" when the name can be a project folder; otherwise the user-facing reason it cannot.
    // Without this a name of "../../etc" scaffolded a tree outside the chosen location and wrote a
    // descriptor whose own folder was not where the hub said it was.
    [[nodiscard]] std::string ValidateProjectName( const std::string& name );

    // One template folder found under `<engine-root>/Templates/`.
    //
    // Templates used to be three C++ structs compiled into the launcher, which made the SET OF
    // TEMPLATES a property of the launcher BUILD rather than of the engine install it launches:
    // adding one meant a recompile, and a template could not carry content at all, because the
    // launcher links no engine code and cannot author a scene or a material. A folder with a
    // manifest and a byte-copied payload has neither problem — adding a template is dropping a
    // folder in.
    struct TemplateEntry
    {
        std::string                       Id;        // the folder name; not repeated inside the manifest
        std::string                       Directory; // <engine-root>/Templates/<Id>
        Common::Project::TemplateManifest Manifest;
        std::string ThumbnailPath; // <Directory>/Media/Thumbnail.png when present; "" otherwise
    };

    // What a scan of `<engine-root>/Templates/` found, INCLUDING what it could not read.
    //
    // A template folder that does not parse is a Refusal carrying the parser's own words, never a
    // silent skip: a silent skip means a template that exists on disk and nowhere on screen, and
    // the person who mistyped its manifest has no way to find that out (L2 §3.4).
    struct TemplateScan
    {
        // Ordered by Category, then SortKey, then DisplayName — never by folder order. The order
        // IS the grouping the New Project screen draws.
        std::vector<TemplateEntry> Templates;
        std::vector<std::string>   Refusals; // one per folder that did not load, verbatim
    };

    [[nodiscard]] TemplateScan ScanTemplates( const std::string& engineRoot );

    // Scaffolds a project and returns the path of the .deproj it wrote.
    //
    // Every step is checked and every failure is named. It also CLEANS UP after itself: a create
    // that fails half way used to leave a folder tree the engine would never open sitting in the
    // user's chosen location, and the next attempt with the same name then refused because "the
    // folder is not empty". The cleanup only ever removes a directory this call created.
    //
    // The template's `Payload/` is copied BYTE FOR BYTE — no name substitutions anywhere (L2 §2.2).
    // A `.desce` is JSON full of GUID references to materials; a textual replacement inside one is
    // a way to break a reference, not a way to personalise a scene. The project's name lives in
    // exactly one generated file, the .deproj.
    [[nodiscard]] Common::ResultStr<std::string> CreateProject( const std::string&   parentDirectory,
                                                                const std::string&   name,
                                                                const TemplateEntry& projectTemplate,
                                                                const std::string&   engineVersion );
} // namespace Hub
