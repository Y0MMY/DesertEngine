#include "ProjectContext.hpp"

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Core/Version.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>

namespace Desert::Project
{
    namespace
    {
        std::optional<ProjectFile> s_Current;
        std::string                s_FilePath;

        std::string RegistryFile()
        {
            return ProjectContext::ConfigDirectory() + "/projects.json";
        }
    } // namespace

    std::string ProjectContext::ConfigDirectory()
    {
        const char* home = std::getenv( "HOME" );
#ifdef DESERT_PLATFORM_WINDOWS
        if ( !home )
            home = std::getenv( "USERPROFILE" );
#endif
        std::filesystem::path dir = std::filesystem::path( home ? home : "." ) / ".desertengine";
        std::error_code       ec;
        std::filesystem::create_directories( dir, ec );
        return dir.string();
    }

    bool ProjectContext::Open( const std::string& deprojPath, RecordInRecent record )
    {
        // Disk first (dev, loose .deproj), else a packaged game serves the descriptor from a mounted .dpak.
        if ( !Common::Utils::FileSystem::Exists( deprojPath ) )
        {
            LOG_ERROR( "[Project] File not found: {}", deprojPath );
            return false;
        }
        const bool onDisk = std::filesystem::exists( deprojPath );

        // An empty .deproj is as unusable as an unreadable one — both refuse here, before the parse.
        const auto rawRead = Common::Utils::FileSystem::ReadFileContent( deprojPath );
        if ( !rawRead || rawRead.GetValue().empty() )
        {
            LOG_ERROR( "[Project] Cannot read {}", deprojPath );
            return false;
        }

        auto parsed = Common::Project::ReadProjectFile( rawRead.GetValue() );
        if ( !parsed.IsSuccess() )
        {
            LOG_ERROR( "[Project] {}: {}", deprojPath, parsed.GetError() );
            return false;
        }

        s_Current  = parsed.ExtractValue();
        s_FilePath = std::filesystem::absolute( deprojPath ).string();

        // THE decoupling step: point every engine content path (and the Cooked/ cache) at this project.
        // Must happen before any subsystem reads the constants — callers open the project while parsing
        // --project, before the engine spins up.
        const std::filesystem::path projectDir = std::filesystem::path( s_FilePath ).parent_path();
        Common::Constants::Path::SetProjectRoot( projectDir, s_Current->AssetsRoot );

        // Make sure the standard content folders exist (a freshly created project has only a few). Skipped for
        // a packaged game (opened from a read-only .dpak) — its content lives in the archive, not on disk.
        if ( onDisk )
        {
            // The census lives beside the format (desert-shared ProjectFormat.hpp) — the same rows the
            // launcher scaffolds a new project from, so "what a project has" cannot fork between creator
            // and opener. ASSETS_PATH was just remapped above, so each row lands inside this project;
            // Tests/Engine/ProjectFormat asserts every row equals the Constants::Path global it answers to.
            std::error_code ec;
            for ( const std::string_view folder : Common::Project::StandardContentFolders )
                std::filesystem::create_directories( Common::Constants::Path::ASSETS_PATH / folder, ec );
        }

        // Two independent reasons to stay out of the registry, and they are not the same reason: a
        // packaged game reads its descriptor out of a mounted .dpak and is not on this machine's
        // disk at all, and a headless capture run is on disk but is not a person opening a project.
        if ( onDisk && record == RecordInRecent::Yes )
            RegisterRecent( s_FilePath );
        LOG_INFO( "[Project] Opened '{}' ({}) — assets root: {}", s_Current->Name, s_FilePath,
                  Common::Constants::Path::ASSETS_PATH.string() );
        return true;
    }

    bool ProjectContext::Save()
    {
        if ( !s_Current || s_FilePath.empty() )
            return false;
        // Stamped on the way out, at the one place the descriptor is written: EngineVersion means
        // "the build that last wrote this file", so deriving it anywhere else would make it a claim
        // about something other than this write. A project the launcher created and nobody has
        // saved yet keeps the version the launcher put there.
        s_Current->EngineVersion = Common::Version::Full();
        // Atomic (write-then-rename), because the .deproj is the one file without which the project
        // does not open at all: the plain primitive truncates in place, so a write interrupted half
        // way used to leave zero bytes where the descriptor was.
        if ( !Common::Utils::FileSystem::WriteContentToFileAtomic(
                  std::filesystem::path( s_FilePath ), Common::Project::WriteProjectFile( *s_Current ) ) )
        {
            LOG_ERROR( "[Project] Could not save {} — the file on disk is unchanged", s_FilePath );
            return false;
        }
        LOG_INFO( "[Project] Saved {}", s_FilePath );
        return true;
    }

    bool ProjectContext::SetDefaultScene( const std::string& sceneRelPath )
    {
        if ( !s_Current )
            return false;
        s_Current->DefaultScene = sceneRelPath;
        return Save();
    }

    bool ProjectContext::HasProject()
    {
        return s_Current.has_value();
    }

    const ProjectFile& ProjectContext::Current()
    {
        return *s_Current;
    }

    std::string ProjectContext::Directory()
    {
        return s_FilePath.empty() ? std::string()
                                  : std::filesystem::path( s_FilePath ).parent_path().string();
    }

    std::string ProjectContext::FilePath()
    {
        return s_FilePath;
    }

    std::string ProjectContext::DefaultScenePath()
    {
        if ( !s_Current || s_Current->DefaultScene.empty() )
            return {};
        return ( std::filesystem::path( Directory() ) / s_Current->DefaultScene ).string();
    }

    Common::Project::ProjectsRegistry ProjectContext::RecentProjects()
    {
        if ( !std::filesystem::exists( RegistryFile() ) )
            return {};

        const auto raw = Common::Utils::FileSystem::ReadFileContent( RegistryFile() );
        if ( !raw || raw.GetValue().empty() )
            return {};
        auto parsed = Common::Project::ReadProjectsRegistry( raw.GetValue() );
        if ( !parsed.IsSuccess() )
        {
            // Refusing quietly here looked like "my projects vanished" — name the file and the reason.
            LOG_ERROR( "[Project] {}: {}", RegistryFile(), parsed.GetError() );
            return {};
        }
        // The reader migrates a registry from before LastOpened; the write below then puts the
        // current shape on disk, so the file upgrades the first time any project is opened.
        return parsed.ExtractValue();
    }

    void ProjectContext::RegisterRecent( const std::string& deprojPath )
    {
        // The policy — most recent first, unique, no cap, LastOpened stamped — is ONE function in
        // desert-shared now, called by this side and by the launcher. It used to be two copies over
        // one shared file, which is how both of them ended up carrying the same silent cap of ten:
        // lifting either alone would have had the other erase what it kept.
        auto registry = RecentProjects();
        Common::Project::PromoteRecent( registry, deprojPath, Common::Project::UnixNow() );

        // Atomic (write-then-rename): this file is shared with the Project Hub, and an interrupted
        // in-place write left a torn projects.json that neither side could parse — every recent
        // project gone over one crash at the wrong moment. On failure the registry simply keeps its
        // previous list, which is the right outcome for a convenience file: name it and move on.
        if ( !Common::Utils::FileSystem::WriteContentToFileAtomic(
                  std::filesystem::path( RegistryFile() ), Common::Project::WriteProjectsRegistry( registry ) ) )
            LOG_ERROR( "[Project] Could not update the recent-projects registry {} — it keeps its "
                       "previous contents",
                       RegistryFile() );
    }
} // namespace Desert::Project
