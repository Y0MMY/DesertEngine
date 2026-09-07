#include "Projects.hpp"

#include "Files.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string_view>

namespace Hub
{
    namespace fs = std::filesystem;

    std::string ProjectThumbnailPath( const std::string& deprojPath )
    {
        const fs::path  candidate = fs::path( deprojPath ).parent_path() / ".thumbnail.png";
        std::error_code ec;
        // Checked against the disk, not derived from the path. The tile is a picture first, so
        // "there is a file here" and "there could be a file here" are the two states it draws
        // differently, and the second one must never be mistaken for the first.
        return fs::exists( candidate, ec ) ? candidate.string() : std::string();
    }

    ProjectEntry ResolveProjectEntry( const std::string& deprojPath )
    {
        ProjectEntry entry;
        entry.Path          = deprojPath;
        entry.Name          = fs::path( deprojPath ).stem().string(); // the fallback, replaced below
        entry.ThumbnailPath = ProjectThumbnailPath( deprojPath );

        std::error_code ec;
        if ( !fs::exists( deprojPath, ec ) )
        {
            // Short on purpose: this is drawn on a 240 px tile beside a Remove button, and a
            // sentence that elides to "the project file is gon..." says less than two words do.
            // The PATH is on the same tile, where the picture would have been.
            entry.Trouble = "file not found";
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
        const Common::Project::ProjectFile descriptor = parsed.ExtractValue();
        if ( !descriptor.Name.empty() )
            entry.Name = descriptor.Name;
        entry.Description = descriptor.Description;
        return entry;
    }

    ProjectEntry ResolveProjectEntry( const Common::Project::ProjectRecord& record )
    {
        ProjectEntry entry = ResolveProjectEntry( record.Path );
        entry.LastOpened   = record.LastOpened;
        return entry;
    }

    std::vector<ProjectEntry> ResolveProjectEntries( const Common::Project::ProjectsRegistry& registry )
    {
        std::vector<ProjectEntry> entries;
        entries.reserve( registry.Projects.size() );
        for ( const Common::Project::ProjectRecord& record : registry.Projects )
            entries.push_back( ResolveProjectEntry( record ) );
        return entries;
    }

    std::string RelativeTime( long long lastOpenedUnix, long long nowUnix )
    {
        // 0 means the registry never recorded a time for this entry — every line migrated from the
        // flat format is like that. The tile then shows nothing, which is the truth; a date
        // computed from 0 would read "Jan 1 1970" on a project someone opened this morning.
        if ( lastOpenedUnix <= 0 )
            return {};

        const long long seconds = nowUnix - lastOpenedUnix;
        // A clock that moved backwards (a machine that resynced, a file copied from elsewhere) is
        // not a future project. Say nothing rather than "in 3 hours".
        if ( seconds < 0 )
            return {};

        if ( seconds < 60 )
            return "Just now";
        if ( seconds < 3600 )
        {
            const long long minutes = seconds / 60;
            return std::to_string( minutes ) + ( minutes == 1 ? " minute ago" : " minutes ago" );
        }
        if ( seconds < 24 * 3600 )
        {
            const long long hours = seconds / 3600;
            return std::to_string( hours ) + ( hours == 1 ? " hour ago" : " hours ago" );
        }
        if ( seconds < 2 * 24 * 3600 )
            return "Yesterday";
        if ( seconds < 7 * 24 * 3600 )
            return std::to_string( seconds / ( 24 * 3600 ) ) + " days ago";

        // Past a week a relative phrase stops being informative ("43 days ago" is not a fact anyone
        // uses), so switch to the date the way a file browser does.
        const std::time_t when = static_cast<std::time_t>( lastOpenedUnix );
        std::tm           local{};
#ifdef _WIN32
        if ( localtime_s( &local, &when ) != 0 )
            return {};
#else
        if ( localtime_r( &when, &local ) == nullptr )
            return {};
#endif
        char buffer[32];
        // "Sep 1" within this year, "Sep 1 2025" once the year differs — a bare "Sep 1" on a
        // two-year-old project reads as this September.
        std::tm           nowLocal{};
        const std::time_t nowTime = static_cast<std::time_t>( nowUnix );
#ifdef _WIN32
        const bool haveNow = localtime_s( &nowLocal, &nowTime ) == 0;
#else
        const bool haveNow = localtime_r( &nowTime, &nowLocal ) != nullptr;
#endif
        const char* format = ( haveNow && nowLocal.tm_year == local.tm_year ) ? "%b %e" : "%b %e %Y";
        if ( std::strftime( buffer, sizeof( buffer ), format, &local ) == 0 )
            return {};

        // %e pads single digits with a space ("Sep  1"); collapse it so the tile reads as a date.
        std::string  text( buffer );
        const size_t doubled = text.find( "  " );
        if ( doubled != std::string::npos )
            text.erase( doubled, 1 );
        return text;
    }

    int GridColumns( float contentWidth )
    {
        // L2 §6.1, verbatim. The floor and the minimum are one rule, not a rule and a guard: at the
        // minimum window (860 wide, 582 px of content) floor gives 2 on its own, so the two agree
        // instead of one rescuing the other.
        const int columns = static_cast<int>( std::floor( contentWidth / 250.0f ) );
        return columns < 2 ? 2 : columns;
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

    TemplateScan ScanTemplates( const std::string& engineRoot )
    {
        TemplateScan scan;
        if ( engineRoot.empty() )
        {
            scan.Refusals.push_back( "No engine install is known, so there is nowhere to look for templates." );
            return scan;
        }

        const fs::path  templatesDirectory = fs::path( engineRoot ) / "Templates";
        std::error_code ec;
        if ( !fs::exists( templatesDirectory, ec ) )
        {
            scan.Refusals.push_back( templatesDirectory.string() + " does not exist - this engine install ships "
                                                                   "no project templates." );
            return scan;
        }

        fs::directory_iterator it( templatesDirectory, ec );
        if ( ec )
        {
            scan.Refusals.push_back( "Could not list " + templatesDirectory.string() + ": " + ec.message() );
            return scan;
        }

        for ( const fs::directory_entry& folder : it )
        {
            std::error_code entryEc;
            if ( !folder.is_directory( entryEc ) )
                continue;

            const fs::path manifestPath = folder.path() / "template.json";
            if ( !fs::exists( manifestPath, entryEc ) )
            {
                // A folder under Templates/ with no manifest is a mistake, not a convention: say so
                // rather than pretending the folder is not there.
                scan.Refusals.push_back( folder.path().filename().string() + ": no template.json" );
                continue;
            }

            const auto raw = ReadTextFile( manifestPath );
            if ( !raw.IsSuccess() )
            {
                scan.Refusals.push_back( folder.path().filename().string() + ": " + raw.GetError() );
                continue;
            }

            auto parsed = Common::Project::ReadTemplateManifest( raw.GetValue() );
            if ( !parsed.IsSuccess() )
            {
                // The PARSER's own words, so the author of the manifest reads the same sentence the
                // format would have told them.
                scan.Refusals.push_back( manifestPath.string() + ": " + parsed.GetError() );
                continue;
            }

            TemplateEntry entry;
            entry.Id        = folder.path().filename().string();
            entry.Directory = folder.path().string();
            entry.Manifest  = parsed.ExtractValue();
            if ( entry.Manifest.DisplayName.empty() )
                entry.Manifest.DisplayName = entry.Id; // a nameless tile is worse than the folder name

            const fs::path thumbnail = folder.path() / "Media" / "Thumbnail.png";
            if ( fs::exists( thumbnail, entryEc ) )
                entry.ThumbnailPath = thumbnail.string();

            scan.Templates.push_back( std::move( entry ) );
        }

        // Category, then SortKey, then the display name — and never the order the filesystem
        // happened to return, which is not a thing a user can reason about.
        //
        // Category leads because the New Project screen draws a heading whenever it changes, so the
        // sort IS the grouping; uncategorised templates ("" sorts first) stay at the top with no
        // heading above them. With no template declaring a category — which is every template today
        // — this reduces exactly to SortKey then name, which is how "Blank" comes first without
        // being alphabetically first.
        std::sort( scan.Templates.begin(), scan.Templates.end(),
                   []( const TemplateEntry& a, const TemplateEntry& b )
                   {
                       if ( a.Manifest.Category != b.Manifest.Category )
                           return a.Manifest.Category < b.Manifest.Category;
                       if ( a.Manifest.SortKey != b.Manifest.SortKey )
                           return a.Manifest.SortKey < b.Manifest.SortKey;
                       return a.Manifest.DisplayName < b.Manifest.DisplayName;
                   } );
        std::sort( scan.Refusals.begin(), scan.Refusals.end() );
        return scan;
    }

    Common::ResultStr<std::string> CreateProject( const std::string& parentDirectory, const std::string& name,
                                                  const TemplateEntry& projectTemplate,
                                                  const std::string&   engineVersion )
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

        Common::Project::ProjectFile deproj;
        deproj.Name          = name;
        deproj.DefaultScene  = projectTemplate.Manifest.DefaultScene;
        deproj.EngineVersion = engineVersion;

        // 1. The payload, byte for byte. Copied FIRST, so the census pass below only has to fill in
        //    what the template did not already provide — and so a template that ships a folder does
        //    not race the census for it.
        const fs::path payload = fs::path( projectTemplate.Directory ) / "Payload";
        if ( fs::exists( payload, ec ) )
        {
            std::error_code copyEc;
            fs::copy( payload, root,
                      fs::copy_options::recursive | fs::copy_options::copy_symlinks |
                           fs::copy_options::overwrite_existing,
                      copyEc );
            if ( copyEc )
                return cleanUp( "Could not copy the template payload " + payload.string() + " into " +
                                root.string() + ": " + copyEc.message() );
        }

        // 2. The standard folders, from the shared census (ProjectFormat.hpp) — the same rows the
        //    engine re-creates on open. `deproj.AssetsRoot` is the root they live under, so the
        //    descriptor and the disk cannot disagree either.
        //
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

        // 3. The descriptor, written by the same serializer the Editor parses with — the struct is
        //    the format, so a project name containing a quote or a backslash is escaped instead of
        //    corrupting the file. The descriptor IS the project: a create that cannot write it has
        //    produced a folder tree the engine will never open, so it fails loudly instead of
        //    returning a path to nothing.
        const fs::path deprojPath = root / ( name + ".deproj" );
        if ( const auto written = WriteTextFile( deprojPath, Common::Project::WriteProjectFile( deproj ) );
             !written.IsSuccess() )
            return cleanUp( "Could not write the project descriptor " + deprojPath.string() + ": " +
                            written.GetError() );

        // 4. A template that names a default scene must have shipped it. Caught HERE rather than by
        //    the Editor an instant later: the launcher knows both the manifest and the tree it just
        //    laid down, and "Create & Open" that opens into a missing-scene error is a failure the
        //    launcher had every fact needed to prevent.
        if ( !deproj.DefaultScene.empty() && !fs::exists( root / deproj.DefaultScene, ec ) )
            return cleanUp( "The template '" + projectTemplate.Id + "' names " + deproj.DefaultScene +
                            " as its default scene, but its payload does not contain that file." );

        return Common::MakeSuccess( deprojPath.string() );
    }
} // namespace Hub
