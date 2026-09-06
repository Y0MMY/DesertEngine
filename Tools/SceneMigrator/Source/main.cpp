// Thin entry point. Everything the tool does — collect, parse, migrate, report, write back, exit
// code — is RunSceneMigrator in MigratorMain.cpp, behind a callable signature so the write path is
// compiled and driven by Tests/Tools/SceneMigratorWritePath the way the pure migrations already are.
// The one thing main() owns is turning argv into strings and the return into the process exit code.

#include "MigratorMain.hpp"

#include <iostream>
#include <string>
#include <vector>

int main( int argc, char** argv )
{
    const std::vector<std::string> args( argv + 1, argv + argc );
    return Desert::Migration::RunSceneMigrator( args, std::cout, std::cerr );
}
