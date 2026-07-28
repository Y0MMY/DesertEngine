#include "ProjectContext.hpp"

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/Constants.hpp>

#include <rflcpp/rfl/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace Desert::Project
{
    namespace
    {
        // Shape of <config>/projects.json — shared with the Project Hub, which reads/writes the same
        // trivial structure without rfl. Keep the field name in sync with Tools/ProjectHub.
        struct ProjectsRegistry
        {
            std::vector<std::string> Projects;
        };

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
        if ( !std::filesystem::exists( deprojPath ) )
        {
            LOG_ERROR( "[Project] File not found: {}", deprojPath );
            return false;
        }

        const std::string raw = Common::Utils::FileSystem::ReadFileContent( deprojPath );
        if ( raw.empty() )
        {
            LOG_ERROR( "[Project] Cannot read {}", deprojPath );
            return false;
        }

        auto parsed = rfl::json::read<ProjectFile>( raw );
        if ( !parsed.has_value() )
        {
            LOG_ERROR( "[Project] Corrupt .deproj {}: {}", deprojPath, parsed.error().what() );
            return false;
        }

        s_Current  = parsed.value();
        s_FilePath = std::filesystem::absolute( deprojPath ).string();

        // THE decoupling step: point every engine content path (and the Cooked/ cache) at this project.
        // Must happen before any subsystem reads the constants — callers open the project while parsing
        // --project, before the engine spins up.
        const std::filesystem::path projectDir = std::filesystem::path( s_FilePath ).parent_path();
        Common::Constants::Path::SetProjectRoot( projectDir, s_Current->AssetsRoot );

        // Make sure the standard content folders exist (a freshly created project has only a few).
        {
            namespace P = Common::Constants::Path;
            std::error_code ec;
            for ( const auto& dir : { P::MESH_PATH, P::MATERIAL_PATH, P::TEXTUREDIR_PATH, P::SCENE_PATH,
                                      P::PREFAB_PATH, P::SCRIPT_PATH, P::COLLECTIONS_PATH } )
                std::filesystem::create_directories( dir, ec );
        }

        RegisterRecent( s_FilePath );
        LOG_INFO( "[Project] Opened '{}' ({}) — assets root: {}", s_Current->Name, s_FilePath,
                  Common::Constants::Path::ASSETS_PATH.string() );
        return true;
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

        const std::string raw = Common::Utils::FileSystem::ReadFileContent( RegistryFile() );
        if ( raw.empty() )
            return {};
        auto parsed = rfl::json::read<ProjectsRegistry>( raw );
        return parsed.has_value() ? parsed.value().Projects : std::vector<std::string>{};
    }

    void ProjectContext::RegisterRecent( const std::string& deprojPath )
    {
        auto projects = RecentProjects();
        projects.erase( std::remove( projects.begin(), projects.end(), deprojPath ), projects.end() );
        projects.insert( projects.begin(), deprojPath );
        if ( projects.size() > 10 )
            projects.resize( 10 );

        Common::Utils::FileSystem::WriteContentToFile(
             std::filesystem::path( RegistryFile() ),
             rfl::json::write( ProjectsRegistry{ std::move( projects ) } ) );
    }
} // namespace Desert::Project
