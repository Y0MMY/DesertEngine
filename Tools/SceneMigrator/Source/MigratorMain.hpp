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

#include <ostream>
#include <string>
#include <vector>

namespace Desert::Migration
{
    // `args` is the command line without argv[0]: any mix of "--check" and paths (a .desce file or a
    // directory searched recursively). Returns the process exit code: 0 = nothing to do or all
    // raised and written; 1 = a file failed (unreadable, unparseable, or its write failed — the
    // original is left untouched), or --check found work; 2 = usage / nothing found.
    int RunSceneMigrator( const std::vector<std::string>& args, std::ostream& out, std::ostream& err );
} // namespace Desert::Migration
