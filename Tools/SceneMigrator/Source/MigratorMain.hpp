#pragma once

// The WHOLE tool behind a callable signature. The migrations themselves are pure functions and have
// been testable since they moved here — but the tool's FILE loop (collect, parse, report, write
// back, exit code) lived inside main() and was compiled by nothing but the tool, so the one part of
// SceneMigrator that can destroy data was the one part no suite could reach. It did destroy data in
// principle: the write opened the scene itself with trunc, so any failure after the open cost the
// file (see the write site). Tests/Tools/SceneMigratorWritePath drives this function against a write
// that must fail and pins "non-zero exit, original untouched".
//
// The streams are parameters for the same reason the assets root is one in MigrateScene: the caller
// owns them, and a test can read the report back instead of scraping a process's stdout.

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace Desert::Migration
{
    // WHERE A SCENE'S OUTPUTS GO — derived from the SCENE'S OWN PATH, never from the working directory
    // the tool happens to be launched from.
    //
    // THE DEFECT THIS IS. The v11 -> v12 raise creates a `.demat` beside the scene and writes its
    // assets-root-relative name into the scene. The write site used to resolve that name against
    // `Constants::Path::ASSETS_PATH`, which with no project open is the relative `Resources/Assets/` —
    // i.e. it resolved against the CURRENT DIRECTORY. Run from the repository root over
    // `Editor/Resources/Assets/Scenes/Autosave/X.desce`, it created a brand-new `Resources/Assets/`
    // tree AT THE REPOSITORY ROOT and put the material there, while the scene named it relative to the
    // root it actually lives under. Two DIFFERING files, one name, one relative path, two roots — and
    // which one the engine loads decided by where somebody stood when they ran a tool.
    //
    // THE RELATION, which is what is fixed rather than the site: the file a migration produces and the
    // path it writes into the scene must be measured against ONE root, and that root is a property of
    // the scene, not of the process. `Common::Constants::Path::RootForContentPath` answers it by
    // reading the same census row the engine's own `Dir(ContentDir::Scene)` is derived from, so there
    // is no second spelling of the layout here to drift from it.
    //
    // A scene that is NOT inside a `Scenes/` folder — a fixture in a temp directory, a file handed over
    // by hand — has no census answer, and its own directory is used. That is not a fallback to the
    // working directory in disguise: it still says "beside the scene", it is still derived from the
    // scene's path, and for a bare `x.desce` the scene's directory IS the working directory, which is
    // then the honest answer rather than an accident.
    //
    // LEXICAL and pure: no disk is consulted, so the answer is the same for a scene about to be written
    // as for one that exists, and it cannot change under a concurrent run.
    std::filesystem::path SceneOutputRoot( const std::filesystem::path& scenePath );

    // `args` is the command line without argv[0]: any mix of "--check" and paths (a .desce file or a
    // directory searched recursively). Returns the process exit code: 0 = nothing to do or all
    // raised and written; 1 = a file failed (unreadable, unparseable, or its write failed — the
    // original is left untouched), or --check found work; 2 = usage / nothing found.
    int RunSceneMigrator( const std::vector<std::string>& args, std::ostream& out, std::ostream& err );
} // namespace Desert::Migration
