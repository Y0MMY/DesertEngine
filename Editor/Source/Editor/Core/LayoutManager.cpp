#include "LayoutManager.hpp"

#include "EditorPreferences.hpp"

#include <Common/Core/Logger.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <ImGui/imgui.h>

#include <algorithm>
#include <cctype>

namespace Desert::Editor
{
    std::filesystem::path LayoutManager::LayoutsDir()
    {
        return std::filesystem::path( EditorPreferences::ConfigDirectory() ) / "Layouts";
    }

    std::string LayoutManager::Sanitize( const std::string& name )
    {
        std::string out;
        for ( char c : name )
        {
            const unsigned char u = static_cast<unsigned char>( c );
            if ( std::isalnum( u ) || c == ' ' || c == '_' || c == '-' )
                out += c;
        }
        while ( !out.empty() && out.front() == ' ' )
            out.erase( out.begin() );
        while ( !out.empty() && out.back() == ' ' )
            out.pop_back();
        return out;
    }

    std::vector<std::string> LayoutManager::List()
    {
        std::vector<std::string> names;
        std::error_code          ec;
        for ( const auto& entry : std::filesystem::directory_iterator( LayoutsDir(), ec ) )
        {
            if ( entry.path().extension() == ".ini" )
                names.push_back( entry.path().stem().string() );
        }
        std::sort( names.begin(), names.end() );
        return names;
    }

    bool LayoutManager::Save( const std::string& name )
    {
        const std::string clean = Sanitize( name );
        if ( clean.empty() )
            return false;

        std::size_t       size = 0;
        const char*       data = ::ImGui::SaveIniSettingsToMemory( &size );
        const std::string ini( data ? data : "", size );

        std::error_code ec;
        std::filesystem::create_directories( LayoutsDir(), ec );
        const auto path = LayoutsDir() / ( clean + ".ini" );
        if ( const auto written = Common::Utils::FileSystem::WriteContentToFileAtomic( path, ini ); !written )
        {
            // The `bool` used to answer only "was the name usable?", so a layout that never reached
            // the disk was reported saved and then simply was not in the list next time.
            LOG_ERROR( "[Layout] '{}' was not saved: {}", path.string(), written.GetError() );
            return false;
        }
        return true;
    }

    bool LayoutManager::Load( const std::string& name )
    {
        const auto path = LayoutsDir() / ( Sanitize( name ) + ".ini" );
        if ( !std::filesystem::exists( path ) )
            return false;
        const auto ini = Common::Utils::FileSystem::ReadFileContent( path.string() );
        if ( !ini )
            return false; // an unreadable layout used to "load" as a no-op empty ini
        ::ImGui::LoadIniSettingsFromMemory( ini.GetValue().c_str(), ini.GetValue().size() );
        return true;
    }

    void LayoutManager::Delete( const std::string& name )
    {
        std::error_code ec;
        std::filesystem::remove( LayoutsDir() / ( Sanitize( name ) + ".ini" ), ec );
    }
} // namespace Desert::Editor
