// THE CENSUS OF WHAT SURVIVES A RELOAD, and the defect it exists for.
//
// VisibilityComponent had no entry in ComponentRegistry.cpp. The outliner's eye therefore worked
// perfectly for exactly as long as the process lived: hide an object, save, reopen the scene, and it is
// visible again — with no error, no warning and no mark. The user's own edit was discarded by a table
// they cannot see. The lock one row up in the same panel had the identical defect and it was found by
// someone fixing the lock, not by any check.
//
// So "is this component persisted?" stops being something a reader has to notice. Every
// `<Something>Component` in the ECS headers must fall into exactly one of four buckets, and the fourth
// two are WRITTEN DOWN HERE WITH A REASON:
//
//   1. registered in ComponentRegistry.cpp        — the ordinary answer;
//   2. written by EntitySerializer.cpp            — the entity RECORD's own fields (id, parent, Tag,
//                                                   Prefab, Transform), which are not component blocks;
//   3. kTransient                                 — deliberately not persisted, with the reason;
//   4. kOwed                                      — NOT persisted and that IS a defect, with the task
//                                                   that owes it. Same shape as SettingConsumers next
//                                                   door: a gap that is named is a gap somebody can be
//                                                   held to; a gap that is merely absent is invisible.
//
// A component in none of them fails this suite, which is the whole point — adding a component is the
// moment the decision is cheap, and it is the only moment anybody is thinking about it.
//
// NOTHING IS LINKED FROM THE ENGINE. The question is "does this table name that type", a relation
// between two files, so both are read as TEXT (the same argument ComponentPools makes next door). It
// needs no GPU, no scene and no asset manager, and it cannot go stale against a build.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // The repository root, found by walking up from wherever the binary was started — the same approach
    // ComponentPools and SettingConsumers use, so none of them has to be run from one exact directory.
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/ECS/Components.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadFile( const std::string& path )
    {
        std::ifstream in( path );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Line comments removed. Without this a component NAMED in prose — and these headers explain
    // themselves at length — is counted as declared, and a registration that was commented out to see
    // what breaks is counted as present.
    std::string WithoutLineComments( const std::string& source )
    {
        std::string out;
        out.reserve( source.size() );
        std::size_t at = 0;
        while ( at < source.size() )
        {
            const std::size_t line = source.find( '\n', at );
            const std::size_t end  = line == std::string::npos ? source.size() : line;
            std::string       text = source.substr( at, end - at );
            if ( const auto comment = text.find( "//" ); comment != std::string::npos )
                text = text.substr( 0, comment );
            out += text;
            out += '\n';
            at = end == source.size() ? end : end + 1;
        }
        return out;
    }

    bool EndsWithComponent( const std::string& name )
    {
        static const std::string suffix = "Component";
        return name.size() > suffix.size() &&
               name.compare( name.size() - suffix.size(), suffix.size(), suffix ) == 0;
    }

    std::string Identifier( const std::string& source, std::size_t& cursor )
    {
        while ( cursor < source.size() && std::isspace( static_cast<unsigned char>( source[cursor] ) ) )
            ++cursor;
        const std::size_t start = cursor;
        while ( cursor < source.size() &&
                ( std::isalnum( static_cast<unsigned char>( source[cursor] ) ) || source[cursor] == '_' ) )
            ++cursor;
        return source.substr( start, cursor - start );
    }

    // Every `struct <Something>Component` DECLARED in @p source, with the byte offset of its name so the
    // body can be read back (the marker check below needs it).
    std::map<std::string, std::size_t> DeclaredComponents( const std::string& source )
    {
        static const std::string keyword = "struct ";

        std::map<std::string, std::size_t> found;
        for ( std::size_t at = source.find( keyword ); at != std::string::npos;
              at             = source.find( keyword, at + 1 ) )
        {
            std::size_t       cursor = at + keyword.size();
            const std::string name   = Identifier( source, cursor );
            if ( !EndsWithComponent( name ) )
                continue;

            // A forward declaration (`struct XComponent;`) declares no fields and stores nothing.
            std::size_t probe = cursor;
            while ( probe < source.size() && std::isspace( static_cast<unsigned char>( source[probe] ) ) )
                ++probe;
            if ( probe >= source.size() || source[probe] != '{' )
                continue;

            found[name] = probe;
        }
        return found;
    }

    // Every `ECS::<Something>Component` named anywhere in @p source.
    std::set<std::string> ComponentsNamedIn( const std::string& source )
    {
        static const std::string qualifier = "ECS::";

        std::set<std::string> found;
        for ( std::size_t at = source.find( qualifier ); at != std::string::npos;
              at             = source.find( qualifier, at + 1 ) )
        {
            std::size_t       cursor = at + qualifier.size();
            const std::string name   = Identifier( source, cursor );
            if ( EndsWithComponent( name ) )
                found.insert( name );
        }
        return found;
    }

    // The headers that declare components. Listed rather than globbed: a new file here is a decision, and
    // this suite failing to know about it is exactly the silence it exists to remove — the four
    // sky/cloud components already live outside Components.hpp.
    const std::vector<std::string> kComponentHeaders = {
         "Desert/Desert/Source/Engine/ECS/Components.hpp",
         "Desert/Desert/Source/Engine/ECS/SkyAtmosphereComponent.hpp",
         "Desert/Desert/Source/Engine/ECS/ExponentialHeightFogComponent.hpp",
         "Desert/Desert/Source/Engine/ECS/VolumetricCloudComponent.hpp",
         "Desert/Desert/Source/Engine/ECS/HeroCloudComponent.hpp",
    };

    constexpr const char* kRegistry = "Desert/Desert/Source/Engine/Core/Serialize/ComponentRegistry.cpp";
    constexpr const char* kEntity   = "Desert/Desert/Source/Engine/Core/Serialize/EntitySerializer.cpp";

    // NOT PERSISTED, ON PURPOSE. Each line is the reason, and a name here that is no longer a component
    // fails this suite too — a stale exemption is how a list like this stops meaning anything.
    const std::map<std::string, std::string> kTransient = {
         { "ProjectileComponent",
           "spawned by a script during Play and destroyed on hit or on its own lifetime; there is no "
           "authored projectile to save, and Stop restores the pre-Play snapshot anyway." },
    };

    // NOT PERSISTED, AND THAT IS A DEFECT. Every one of these is authored through the Details panel
    // (Editor/Panels/SceneProperties/ComponentEditorRegistrations.cpp) and silently lost on the next
    // load — the same defect VisibilityComponent had, found by the census that fixed it. They are named
    // here rather than quietly omitted so the count is a number somebody can be asked about.
    const std::map<std::string, std::string> kOwed = {
         { "FoliageComponent", "U11 census: authored by the Foliage paint tool and its Details editor." },
         { "LocomotionComponent", "U11 census: clip names and speed thresholds authored in Details." },
         { "MorphComponent", "U11 census: blendshape weights authored by the Details sliders." },
         { "SocketAttachmentComponent",
           "U11 census: target, bone name and grip offsets authored in Details (also set from Lua)." },
    };
} // namespace

// ── THE CENSUS ─────────────────────────────────────────────────────────────────────────────────────
TEST( ComponentPersistence, EveryEcsComponentIsPersistedOrIsWrittenDownAsNotBeing )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found from the test's working directory";

    std::map<std::string, std::size_t> declared;
    for ( const auto& header : kComponentHeaders )
    {
        const std::string source = ReadFile( root + header );
        ASSERT_FALSE( source.empty() ) << "could not read " << header;
        for ( const auto& [name, body] : DeclaredComponents( WithoutLineComments( source ) ) )
            declared[name] = body;
    }

    // A floor, not a target: it only has to catch a scan that found nothing because a path moved.
    ASSERT_GT( declared.size(), 40u ) << "the component headers were not parsed — " << declared.size()
                                      << " components found, which cannot be right";

    const std::string registrySource = ReadFile( root + kRegistry );
    ASSERT_FALSE( registrySource.empty() ) << "could not read " << kRegistry;
    const std::set<std::string> registered = ComponentsNamedIn( WithoutLineComments( registrySource ) );

    const std::string entitySource = ReadFile( root + kEntity );
    ASSERT_FALSE( entitySource.empty() ) << "could not read " << kEntity;
    const std::set<std::string> byRecord = ComponentsNamedIn( WithoutLineComments( entitySource ) );

    std::vector<std::string> unclassified;
    for ( const auto& [name, body] : declared )
    {
        (void)body;
        if ( registered.count( name ) != 0 || byRecord.count( name ) != 0 )
            continue;
        if ( kTransient.count( name ) != 0 || kOwed.count( name ) != 0 )
            continue;
        unclassified.push_back( name );
    }

    std::string message;
    for ( const auto& name : unclassified )
        message += "\n  " + name;

    EXPECT_TRUE( unclassified.empty() )
         << "These components are not written by ComponentRegistry.cpp, are not part of the entity "
            "record in EntitySerializer.cpp, and are not written down as transient or owed. Whatever a "
            "user sets on them is silently discarded on the next load — which is how the outliner's "
            "visibility toggle came to be decoration. Register it, or add it to kTransient/kOwed in this "
            "file WITH THE REASON:"
         << message;

    // A stale exemption is worse than none: it reads as a decision somebody took about a type that no
    // longer exists, and it hides the next type that takes the same name.
    for ( const auto& [name, reason] : kTransient )
    {
        (void)reason;
        EXPECT_NE( declared.count( name ), 0u ) << name
                                                << " is listed as deliberately transient but is no "
                                                   "longer an ECS component — delete the entry.";
        EXPECT_EQ( registered.count( name ), 0u ) << name
                                                  << " is listed as deliberately transient but IS "
                                                     "registered — the list and the table disagree.";
    }
    for ( const auto& [name, reason] : kOwed )
    {
        (void)reason;
        EXPECT_NE( declared.count( name ), 0u ) << name
                                                << " is listed as an owed persistence defect but is "
                                                   "no longer an ECS component — delete the entry.";
        EXPECT_EQ( registered.count( name ), 0u ) << name
                                                  << " is listed as an owed persistence defect but "
                                                     "IS registered now — delete the entry.";
    }
}

// ── THE DEFECT THAT LOOKS EXACTLY LIKE THE FIX ─────────────────────────────────────────────────────
//
// MakeMarker serializes an empty object and re-adds the component on load, so the component comes back
// at its STRUCT DEFAULTS. That is correct for a marker (FolderComponent, LockComponent: presence IS the
// state) and silently wrong for anything carrying a field — `Visible = false` would round-trip to
// `true`, and the registration would look like the persistence it is not. Applying MakeMarker to
// VisibilityComponent was the obvious repair and it would have shipped the same defect under a fix.
TEST( ComponentPersistence, EveryMarkerRegistrationIsOnAComponentThatCarriesNothing )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );

    std::map<std::string, std::size_t> declared;
    std::map<std::string, std::string> sourceOf;
    for ( const auto& header : kComponentHeaders )
    {
        const std::string source = WithoutLineComments( ReadFile( root + header ) );
        ASSERT_FALSE( source.empty() ) << "could not read " << header;
        for ( const auto& [name, body] : DeclaredComponents( source ) )
        {
            declared[name] = body;
            sourceOf[name] = source;
        }
    }

    const std::string registrySource = WithoutLineComments( ReadFile( root + kRegistry ) );
    ASSERT_FALSE( registrySource.empty() );

    static const std::string call = "MakeMarker<";

    std::size_t              markers = 0;
    std::vector<std::string> failures;
    for ( std::size_t at = registrySource.find( call ); at != std::string::npos;
          at             = registrySource.find( call, at + 1 ) )
    {
        const std::size_t close = registrySource.find( '>', at );
        ASSERT_NE( close, std::string::npos );
        std::string name = registrySource.substr( at + call.size(), close - at - call.size() );
        if ( const auto colon = name.rfind( ':' ); colon != std::string::npos )
            name = name.substr( colon + 1 );

        // The template's own definition, `ComponentSerializer MakeMarker( std::string key )`, contains no
        // `MakeMarker<` — every hit here is a call site.
        if ( declared.count( name ) == 0 )
            continue;
        ++markers;

        const std::string& source = sourceOf[name];
        const std::size_t  open   = declared[name];
        const std::size_t  end    = source.find( '}', open );
        ASSERT_NE( end, std::string::npos );

        const std::string body = source.substr( open + 1, end - open - 1 );
        if ( body.find_first_not_of( " \t\r\n" ) != std::string::npos )
            failures.push_back( name + " is registered with MakeMarker but its body is not empty: {" + body +
                                "}" );
    }

    EXPECT_GT( markers, 0u ) << "no MakeMarker<> call sites found — this check is asserting nothing";

    std::string message;
    for ( const auto& f : failures )
        message += "\n  " + f;

    EXPECT_TRUE( failures.empty() )
         << "MakeMarker writes an empty object and re-adds the component at its struct defaults, so every "
            "field of these components round-trips to its default while the registration reads as "
            "persistence. Use a maker that writes the fields (MakeFlag for a single bool, MakeReflected "
            "for a data block):"
         << message;
}

// The regression pin for U11 itself. The census above would also catch a removal, but only by naming it
// among a list; this says the sentence.
TEST( ComponentPersistence, TheOutlinerEyeIsWrittenToTheScene )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );

    const std::string registrySource = WithoutLineComments( ReadFile( root + kRegistry ) );
    ASSERT_FALSE( registrySource.empty() );

    EXPECT_NE( registrySource.find( "MakeFlag<ECS::VisibilityComponent>" ), std::string::npos )
         << "VisibilityComponent has no registration, so hiding an object in the outliner lasts until the "
            "scene is next opened and nothing tells the user.";
    EXPECT_NE( registrySource.find( "\"Visibility\"" ), std::string::npos )
         << "the scene key for the visibility flag is gone; every .desce already written carries "
            "\"Visibility\" and would silently load as visible.";
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
