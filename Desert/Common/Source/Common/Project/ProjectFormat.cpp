#include "ProjectFormat.hpp"

#include <rflcpp/rfl/json.hpp>

namespace Common::Project
{
    Common::ResultStr<ProjectFile> ReadProjectFile( const std::string& json )
    {
        auto parsed = rfl::json::read<ProjectFile>( json );
        if ( !parsed.has_value() )
            return Common::MakeFormattedError<ProjectFile>( "Corrupt .deproj: {}", parsed.error().what() );
        return Common::MakeSuccess( std::move( parsed.value() ) );
    }

    std::string WriteProjectFile( const ProjectFile& file )
    {
        return rfl::json::write( file );
    }

    Common::ResultStr<ProjectsRegistry> ReadProjectsRegistry( const std::string& json )
    {
        auto parsed = rfl::json::read<ProjectsRegistry>( json );
        if ( !parsed.has_value() )
            return Common::MakeFormattedError<ProjectsRegistry>( "Corrupt projects.json: {}",
                                                                 parsed.error().what() );
        return Common::MakeSuccess( std::move( parsed.value() ) );
    }

    std::string WriteProjectsRegistry( const ProjectsRegistry& registry )
    {
        return rfl::json::write( registry );
    }
} // namespace Common::Project
