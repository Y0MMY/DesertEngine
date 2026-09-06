#include "Projects.hpp"

#include "Files.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace Hub
{
    namespace fs = std::filesystem;

    ProjectEntry ResolveProjectEntry( const std::string& deprojPath )
    {
        ProjectEntry entry;
        entry.Path = deprojPath;
        entry.Name = fs::path( deprojPath ).stem().string(); // the fallback, replaced below

        std::error_code ec;
        if ( !fs::exists( deprojPath, ec ) )
        {
            entry.Trouble = "the project file is gone from disk";
            return entry;
        }

        const auto raw = ReadTextFile( deprojPath );
        if ( !raw.IsSuccess() )
        {
            entry.Trouble = raw.GetError();
            return entry;
        }

        auto parsed = Common::Project::ReadProjectFile( raw.GetValue() );
        if ( !parsed.IsSuccess() )
        {
            entry.Trouble = parsed.GetError(); // verbatim: the Editor will refuse it for this reason
            return entry;
        }

        // The name shown is the name the ENGINE will show, because both come out of the same
        // descriptor through the same reader. A stem is only what the folder happens to be called.
        const std::string name = parsed.ExtractValue().Name;
        if ( !name.empty() )
            entry.Name = name;
        return entry;
    }

    std::vector<ProjectEntry> ResolveProjectEntries( const std::vector<std::string>& paths )
    {
        std::vector<ProjectEntry> entries;
        entries.reserve( paths.size() );
        for ( const std::string& path : paths )
            entries.push_back( ResolveProjectEntry( path ) );
        return entries;
    }

    void PromoteRecent( std::vector<std::string>& recent, const std::string& deprojPath )
    {
        recent.erase( std::remove( recent.begin(), recent.end(), deprojPath ), recent.end() );
        recent.insert( recent.begin(), deprojPath );
    }

    std::string ValidateProjectName( const std::string& name )
    {
        if ( name.empty() )
            return "The project name is empty.";
        if ( name.find_first_not_of( " \t" ) == std::string::npos )
            return "The project name is only whitespace.";
        if ( name == "." || name == ".." )
            return "'" + name + "' is not a folder name.";
        if ( name.find( '/' ) != std::string::npos || name.find( '\\' ) != std::string::npos )
            return "The project name may not contain a path separator - it names a folder, not a path.";

        // Refused on every platform even though only Windows enforces them: a project created on
        // macOS is meant to open on Windows, and a folder that cannot be created there is a
        // portability bug shipped to the user's disk.
        constexpr std::string_view reserved = "<>:\"|?*";
        for ( const char character : name )
        {
            if ( reserved.find( character ) != std::string_view::npos )
                return std::string( "The project name may not contain '" ) + character + "'.";
            if ( static_cast<unsigned char>( character ) < 0x20 )
                return "The project name contains a control character.";
        }
        if ( name.back() == '.' || name.back() == ' ' )
            return "The project name may not end with a space or a dot (Windows drops them silently).";
        return {};
    }

    const std::vector<ProjectTemplate>& Templates()
    {
        static const std::vector<ProjectTemplate> s_Templates = {
             { "empty", "Empty Project", "Standard content folders - start from scratch.", {}, false, {} },
             { "3d",
               "3D Sandbox",
               "Content folders, a default-scene entry and a starter Lua script.",
               {},
               true,
               { { "Assets/Scripts/Spin.lua", "-- Starter script: rotates the entity it is attached to.\n"
                                              "function OnUpdate(dt)\n"
                                              "    -- self.transform.rotation.y = self.transform.rotation.y + dt\n"
                                              "end\n" },
                 { "README.md", "# 3D Sandbox\n\nCreated with the Desert Project Hub.\n" } } },
             { "2d",
               "2D",
               "Adds a Sprites folder for 2D content.",
               { "Sprites" },
               false,
               { { "README.md", "# 2D Project\n\nCreated with the Desert Project Hub.\n" } } },
        };
        return s_Templates;
    }

    Common::ResultStr<std::string> CreateProject( const std::string& parentDirectory, const std::string& name,
                                                  const ProjectTemplate& projectTemplate )
    {
        if ( parentDirectory.empty() )
            return Common::MakeError<std::string>( "The project location is empty." );
        if ( const std::string reason = ValidateProjectName( name ); !reason.empty() )
            return Common::MakeError<std::string>( reason );

        const fs::path  root = fs::path( parentDirectory ) / name;
        std::error_code ec;
        const bool      rootExisted = fs::exists( root, ec );
        if ( rootExisted && !fs::is_empty( root, ec ) )
            return Common::MakeFormattedError<std::string>( "Folder already exists and is not empty: {}",
                                                            root.string() );

        // Undoes a create that got part way. Only a root this call brought into existence is
        // removed — a user who chose an existing empty folder keeps it.
        const auto cleanUp = [&]( const std::string& reason ) -> Common::ResultStr<std::string>
        {
            if ( !rootExisted )
            {
                std::error_code removeEc;
                fs::remove_all( root, removeEc );
                if ( removeEc )
                    return Common::MakeFormattedError<std::string>(
                         "{} - and the half-created folder {} could not be removed either: {}", reason,
                         root.string(), removeEc.message() );
            }
            return Common::MakeError<std::string>( reason );
        };

        // The folder layout comes from the shared census (ProjectFormat.hpp) — the same rows the
        // engine re-creates on open — plus the template's own. `deproj.AssetsRoot` is the root they
        // are created under, so the descriptor and the disk cannot disagree either.
        Common::Project::ProjectFile deproj;
        deproj.Name = name;
        if ( projectTemplate.SetDefaultScene )
            deproj.DefaultScene = deproj.AssetsRoot + "/Scenes/" + name + ".desce";

        // Checked INSIDE the loop, one row at a time. A single error_code inspected after the loop
        // reported only the last row: std::filesystem clears it on every success, so a failure in
        // the middle was erased by the next folder that happened to work, and the project was
        // declared created with a folder missing.
        const auto makeDirectory = [&]( const fs::path& directory ) -> std::string
        {
            std::error_code directoryEc;
            fs::create_directories( directory, directoryEc );
            if ( directoryEc )
                return "Could not create " + directory.string() + ": " + directoryEc.message();
            return {};
        };

        for ( const std::string_view folder : Common::Project::StandardContentFolders )
            if ( const std::string reason = makeDirectory( root / deproj.AssetsRoot / folder ); !reason.empty() )
                return cleanUp( reason );
        for ( const char* extra : projectTemplate.ExtraFolders )
            if ( const std::string reason = makeDirectory( root / deproj.AssetsRoot / extra ); !reason.empty() )
                return cleanUp( reason );

        for ( const auto& [relative, content] : projectTemplate.Files )
        {
            const fs::path file = root / relative;
            if ( const std::string reason = makeDirectory( file.parent_path() ); !reason.empty() )
                return cleanUp( reason );
            if ( const auto written = WriteTextFile( file, content ); !written.IsSuccess() )
                return cleanUp( "Could not write the starter file " + file.string() + ": " + written.GetError() );
        }

        // Written by the same serializer the Editor parses with — the struct is the format, so a
        // project name containing a quote or a backslash is escaped instead of corrupting the file.
        // The descriptor IS the project: a create that cannot write it has produced a folder tree
        // the engine will never open, so it fails loudly instead of returning a path to nothing.
        const fs::path deprojPath = root / ( name + ".deproj" );
        if ( const auto written = WriteTextFile( deprojPath, Common::Project::WriteProjectFile( deproj ) );
             !written.IsSuccess() )
            return cleanUp( "Could not write the project descriptor " + deprojPath.string() + ": " +
                            written.GetError() );

        return Common::MakeSuccess( deprojPath.string() );
    }
} // namespace Hub
