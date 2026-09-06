#include "Launch.hpp"

#include <DesertShared/LaunchProtocol.hpp>

#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>

#include "Platform/WindowsStrings.hpp"
#else
#include <csignal>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

namespace Hub
{
    namespace fs = std::filesystem;

    namespace
    {
#ifdef _WIN32
        // Where Windows keeps its own tools, asked of the system rather than hardcoded as
        // C:\Windows (D-http §4.5(2): an OS tool is addressed by absolute path, and the absolute
        // path of the Windows directory is whatever this call says it is).
        std::string WindowsDirectory()
        {
            char       buffer[MAX_PATH] = {};
            const UINT written          = ::GetWindowsDirectoryA( buffer, MAX_PATH );
            return ( written > 0 && written < MAX_PATH ) ? std::string( buffer, written )
                                                         : std::string( "C:\\Windows" );
        }

        using Hub::Platform::Widen;

        // Windows has no argv array at the kernel boundary — CreateProcess takes ONE string and the
        // callee splits it again. This is the split's documented inverse (the rule
        // CommandLineToArgvW / the CRT startup implement): quote when the argument contains a space,
        // a tab or a quote; double the backslashes that immediately precede a quote or the closing
        // one; escape a literal quote. It is NOT shell quoting and there is no shell involved — the
        // only reader of this string is the process we are starting.
        std::wstring QuoteArgument( const std::string& argument )
        {
            const std::wstring wide = Widen( argument );
            if ( !wide.empty() && wide.find_first_of( L" \t\"" ) == std::wstring::npos )
                return wide;

            std::wstring quoted = L"\"";
            for ( size_t i = 0;; ++i )
            {
                size_t backslashes = 0;
                while ( i < wide.size() && wide[i] == L'\\' )
                {
                    ++i;
                    ++backslashes;
                }
                if ( i == wide.size() )
                {
                    quoted.append( backslashes * 2, L'\\' ); // they precede the closing quote
                    break;
                }
                if ( wide[i] == L'"' )
                {
                    quoted.append( backslashes * 2 + 1, L'\\' );
                    quoted.push_back( L'"' );
                }
                else
                {
                    quoted.append( backslashes, L'\\' );
                    quoted.push_back( wide[i] );
                }
            }
            quoted.push_back( L'"' );
            return quoted;
        }
#endif
    } // namespace

    LaunchCommand BuildEditorLaunch( const std::string& engineRoot, const std::string& config,
                                     const std::string& deprojPath )
    {
        LaunchCommand command;
#ifdef _WIN32
        command.Program = ( fs::path( engineRoot ) / "build" / "Bin" / config / "Editor.exe" ).string();
        // The Editor resolves Resources/... relative to the working directory — the one thing
        // RunEditor.bat did that a spawn has to keep doing.
        command.WorkingDirectory = ( fs::path( engineRoot ) / "Editor" ).string();
        command.Arguments        = { Common::Launch::kProjectFlag, deprojPath };
#else
        command.Program   = ( fs::path( engineRoot ) / "scripts" / "MacOS" / "RunEditor.sh" ).string();
        command.Arguments = { config, Common::Launch::kProjectFlag, deprojPath };
#endif
        return command;
    }

    LaunchCommand BuildRevealCommand( const std::string& path )
    {
        LaunchCommand command;
#ifdef _WIN32
        // Explorer's /select, syntax glues its argument to the switch and is re-parsed by the shell
        // namespace rather than by CommandLineToArgvW; opening the containing folder is the
        // behaviour that survives a path with a comma or a quote in it.
        command.Program   = WindowsDirectory() + "\\explorer.exe";
        command.Arguments = { fs::path( path ).parent_path().string() };
#else
        command.Program   = "/usr/bin/open";
        command.Arguments = { "-R", path }; // -R reveals the file itself, selected, in its folder
#endif
        return command;
    }

    Common::BoolResultStr SpawnDetached( const LaunchCommand& command )
    {
        // Existence is checked BEFORE the spawn so the refusal names the program instead of an
        // errno. Both platforms' spawn also reports a missing image, but not as usefully.
        std::error_code ec;
        if ( !fs::exists( command.Program, ec ) )
            return Common::MakeFormattedError( "{} does not exist", command.Program );

#ifdef _WIN32
        std::wstring commandLine = QuoteArgument( command.Program );
        for ( const std::string& argument : command.Arguments )
        {
            commandLine.push_back( L' ' );
            commandLine.append( QuoteArgument( argument ) );
        }

        const std::wstring program          = Widen( command.Program );
        const std::wstring workingDirectory = Widen( command.WorkingDirectory );

        STARTUPINFOW startup        = {};
        startup.cb                  = sizeof( startup );
        PROCESS_INFORMATION process = {};

        // lpApplicationName is given explicitly, so the command line is never searched for a
        // program name — the string below is read as arguments only.
        if ( !::CreateProcessW(
                  program.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
                  workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process ) )
            return Common::MakeFormattedError( "could not start {}: Windows error {}", command.Program,
                                               (unsigned long)::GetLastError() );

        // We never wait for it: closing the handles detaches without leaving the child orphaned to
        // a handle nobody will ever read.
        ::CloseHandle( process.hThread );
        ::CloseHandle( process.hProcess );
        return Common::MakeSuccess( true );
#else
        // Reap children automatically. The hub usually exits right after launching the Editor, but
        // "Reveal in Finder" leaves it running — and every /usr/bin/open we never wait for would
        // otherwise pile up as a zombie for as long as the window stays open.
        static const bool reapAutomatically = ( std::signal( SIGCHLD, SIG_IGN ), true );
        (void)reapAutomatically;

        std::vector<std::string> owned;
        owned.reserve( command.Arguments.size() + 1 );
        owned.push_back( command.Program ); // argv[0]
        owned.insert( owned.end(), command.Arguments.begin(), command.Arguments.end() );

        std::vector<char*> argv;
        argv.reserve( owned.size() + 1 );
        for ( std::string& element : owned )
            argv.push_back( element.data() );
        argv.push_back( nullptr );

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init( &actions );
        if ( !command.WorkingDirectory.empty() )
            posix_spawn_file_actions_addchdir_np( &actions, command.WorkingDirectory.c_str() );

        pid_t     child  = 0;
        const int status = posix_spawn( &child, command.Program.c_str(), &actions, nullptr, argv.data(), environ );
        posix_spawn_file_actions_destroy( &actions );

        if ( status != 0 )
            return Common::MakeFormattedError( "could not start {}: {}", command.Program,
                                               std::string( std::strerror( status ) ) );
        return Common::MakeSuccess( true );
#endif
    }
} // namespace Hub
