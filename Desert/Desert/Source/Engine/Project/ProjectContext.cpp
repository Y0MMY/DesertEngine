#include "ProjectContext.hpp"

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/Constants.hpp>

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

    bool ProjectContext::Open( const std::string& deprojPath )
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

        if ( onDisk ) // don't pollute the dev hub's recent-projects list from a packaged game
            RegisterRecent( s_FilePath );
        LOG_INFO( "[Project] Opened '{}' ({}) — assets root: {}", s_Current->Name, s_FilePath,
                  Common::Constants::Path::ASSETS_PATH.string() );
        return true;
    }

    bool ProjectContext::Save()
    {
        if ( !s_Current || s_FilePath.empty() )
            return false;
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

    std::vector<std::string> ProjectContext::RecentProjects()
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
        return parsed.ExtractValue().Projects;
    }

    void ProjectContext::RegisterRecent( const std::string& deprojPath )
    {
        // Most recent first, unique, and NOT capped. There used to be a silent cap of ten here and
        // an identical one in the launcher (Tools/ProjectHub, Hub::PromoteRecent) — this file is
        // shared, so the two had to be lifted together: an uncapped hub next to a capped engine
        // would have dropped everything past the tenth entry the moment any project was opened.
        // The cap was safe to lose only once a dead entry became visible: the launcher now resolves
        // every line against the disk and offers Remove, so the list is curated rather than
        // truncated. Both copies of this policy move into desert-shared with the {Path, LastOpened}
        // registry (L2 §10.4, stage E1); until then they are two places that must say the same thing.
        auto projects = RecentProjects();
        projects.erase( std::remove( projects.begin(), projects.end(), deprojPath ), projects.end() );
        projects.insert( projects.begin(), deprojPath );

        // Atomic (write-then-rename): this file is shared with the Project Hub, and an interrupted
        // in-place write left a torn projects.json that neither side could parse — every recent
        // project gone over one crash at the wrong moment. On failure the registry simply keeps its
        // previous list, which is the right outcome for a convenience file: name it and move on.
        if ( !Common::Utils::FileSystem::WriteContentToFileAtomic(
                  std::filesystem::path( RegistryFile() ),
                  Common::Project::WriteProjectsRegistry(
                       Common::Project::ProjectsRegistry{ std::move( projects ) } ) ) )
            LOG_ERROR( "[Project] Could not update the recent-projects registry {} — it keeps its "
                       "previous contents",
                       RegistryFile() );
    }
} // namespace Desert::Project
