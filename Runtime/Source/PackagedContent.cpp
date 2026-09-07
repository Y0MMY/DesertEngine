#include "PackagedContent.hpp"

#include <Common/Utilities/VFS.hpp>

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <system_error>

namespace Desert::Player
{
    namespace fs = std::filesystem;

    namespace
    {
        // Every refusal message is built here so they cannot drift apart in tone or in what they
        // leave out. The shape is fixed and each part earns its place:
        //   what happened, in one sentence a player understands;
        //   the mount error, which is already "<full path>: <which step failed, with the numbers>"
        //     — the path is in there deliberately, so a support reply never has to ask which folder;
        //   what to DO.
        // The last part is the one that is usually missing and the only one the reader wants.
        std::string RefusalMessage( const std::string& happened, const std::string& mountError,
                                    const std::string& whatToDo )
        {
            return fmt::format( "{}\n\n  What went wrong: {}\n\n{}", happened, mountError, whatToDo );
        }
    } // namespace

    fs::path FindBasePak( const fs::path& dir, const std::string& exeStem, std::vector<fs::path>* ambiguous )
    {
        const fs::path  named = dir / ( exeStem + ".dpak" );
        std::error_code ec;
        if ( fs::exists( named, ec ) )
            return named;
        const fs::path content = dir / "Content.dpak";
        if ( fs::exists( content, ec ) )
            return content;

        std::vector<fs::path> candidates;
        for ( const auto& de : fs::directory_iterator( dir, ec ) )
            if ( de.is_regular_file( ec ) && de.path().extension() == ".dpak" &&
                 de.path().filename().string().rfind( "Patch", 0 ) != 0 )
                candidates.push_back( de.path() );
        if ( candidates.size() == 1 )
            return candidates.front();

        // Several, and none of them named after the exe or after Content. This USED to print a line to
        // stderr and return empty, after which startup fell through to the generic "No game to run —
        // put a Content.dpak here" message, which is advice for the opposite problem: there are too
        // many archives, not none. Reported through the caller now so the player is told the truth.
        if ( candidates.size() > 1 && ambiguous )
        {
            std::sort( candidates.begin(), candidates.end() );
            *ambiguous = std::move( candidates );
        }
        return {};
    }

    ContentMountResult MountPackagedContent( const fs::path& baseDir, const std::string& exeStem )
    {
        ContentMountResult result;

        std::vector<fs::path> ambiguous;
        const fs::path        base = FindBasePak( baseDir, exeStem, &ambiguous );
        if ( !ambiguous.empty() )
        {
            std::string names;
            for ( const auto& candidate : ambiguous )
                names += ( names.empty() ? "" : ", " ) + candidate.filename().string();

            result.ExitCode = kContentArchivesAmbiguous;
            result.Message  = fmt::format(
                 "There are {} game archives next to the program and none of them is named '{}.dpak' or "
                  "'Content.dpak', so there is no way to tell which one is the game.\n\n"
                  "  Folder:   {}\n  Archives: {}\n\n"
                  "  What to do: rename the game's archive to '{}.dpak' so it matches the program, or move "
                  "the ones that do not belong out of this folder.",
                 ambiguous.size(), exeStem, baseDir.string(), names, exeStem );
            return result;
        }

        // No archive at all is NOT a failure: that is the dev tree, where every read is a plain disk
        // read and the loose content is the content. Only a base pak that was FOUND and would not open
        // is fatal.
        if ( !base.empty() )
        {
            if ( const auto mounted = Common::Utils::VFS::MountPak( base ); !mounted )
            {
                result.ExitCode = kContentBaseArchiveFailed;
                result.Message  = RefusalMessage(
                     "The game's content archive could not be opened, so there is nothing to run.",
                     mounted.GetError(),
                     // Two audiences, one message. The second line is for the developer who reaches
                     // this through --project with a damaged BuildContentPak() archive in the project
                     // folder: refusing is still right (the old behaviour left them guessing why an
                     // asset would not update), but "reinstall the game" is not advice anybody can act
                     // on inside a checkout.
                     "  What to do: reinstall the game, or use your store's \"verify/repair files\" option, "
                      "then start it again.\n"
                      "              In a development build, delete the archive instead — the loose files on "
                      "disk are the content." );
                return result;
            }
            result.BasePak = base;
        }

        std::vector<fs::path> patches;
        std::error_code       ec;
        for ( const auto& de : fs::directory_iterator( baseDir, ec ) )
            if ( de.is_regular_file( ec ) && de.path().extension() == ".dpak" &&
                 de.path().filename().string().rfind( "Patch", 0 ) == 0 )
                patches.push_back( de.path() );
        std::sort( patches.begin(), patches.end() );

        for ( const auto& patch : patches )
        {
            if ( const auto mounted = Common::Utils::VFS::MountPak( patch ); !mounted )
            {
                Common::Utils::VFS::Unmount();
                result.BasePak.clear();
                result.Patches.clear();
                result.ExitCode = kContentPatchArchiveFailed;
                result.Message  = RefusalMessage(
                     "An update could not be opened, and the game will not start on the content it was "
                      "meant to replace.",
                     mounted.GetError(),
                     fmt::format( "  Starting anyway would quietly run the version this update fixes, with "
                                    "nothing to show\n  that anything was wrong.\n\n"
                                    "  What to do: download the update again. To play the previous version "
                                    "instead, delete\n              {}\n              and start the game again.",
                                   patch.string() ) );
                return result;
            }
            result.Patches.push_back( patch );
        }

        return result;
    }
} // namespace Desert::Player
