#include "AssetFileOps.hpp"

#include <filesystem>

namespace Desert::Editor::AssetFileOps
{
    namespace fs = std::filesystem;

    std::string UniqueName( const std::string& stem, const std::string& ext,
                            const std::function<bool( const std::string& )>& exists )
    {
        const std::string first = stem + ext;
        if ( !exists( first ) )
            return first;
        for ( int i = 2; i < 100000; ++i )
        {
            const std::string candidate = stem + " " + std::to_string( i ) + ext;
            if ( !exists( candidate ) )
                return candidate;
        }
        return first; // unreachable in practice
    }

    bool Move( const std::string& src, const std::string& destDir, std::string& outNewPath, std::string& error )
    {
        const fs::path s = src;
        std::error_code ec;
        if ( !fs::exists( s, ec ) )
        {
            error = "Source no longer exists.";
            return false;
        }
        if ( fs::equivalent( s.parent_path(), fs::path( destDir ), ec ) )
        {
            error = "Already in this folder.";
            return false;
        }

        const fs::path dst = fs::path( destDir ) / s.filename();
        if ( fs::exists( dst, ec ) )
        {
            error = "A file with that name already exists here.";
            return false;
        }

        fs::rename( s, dst, ec );
        if ( ec )
        {
            // Cross-device (e.g. moving off a mounted volume): rename fails, so copy then delete.
            std::error_code copyEc;
            fs::copy( s, dst, fs::copy_options::recursive, copyEc );
            if ( copyEc )
            {
                error = copyEc.message();
                return false;
            }
            fs::remove_all( s, ec );
        }
        outNewPath = dst.string();
        return true;
    }

    bool Rename( const std::string& src, const std::string& newFileName, std::string& outNewPath,
                 std::string& error )
    {
        if ( newFileName.empty() )
        {
            error = "Name cannot be empty.";
            return false;
        }
        const fs::path  s   = src;
        const fs::path  dst = s.parent_path() / newFileName;
        std::error_code ec;
        if ( fs::exists( dst, ec ) && !fs::equivalent( s, dst, ec ) )
        {
            error = "A file with that name already exists.";
            return false;
        }
        fs::rename( s, dst, ec );
        if ( ec )
        {
            error = ec.message();
            return false;
        }
        outNewPath = dst.string();
        return true;
    }

    bool Duplicate( const std::string& src, std::string& outNewPath, std::string& error )
    {
        const fs::path  s = src;
        std::error_code ec;
        const std::string name =
             UniqueName( s.stem().string(), s.extension().string(),
                         [&]( const std::string& n ) { return fs::exists( s.parent_path() / n ); } );
        const fs::path dst = s.parent_path() / name;
        fs::copy( s, dst, fs::copy_options::recursive, ec );
        if ( ec )
        {
            error = ec.message();
            return false;
        }
        outNewPath = dst.string();
        return true;
    }

    bool Delete( const std::string& path, std::string& error )
    {
        std::error_code ec;
        fs::remove_all( path, ec );
        if ( ec )
        {
            error = ec.message();
            return false;
        }
        return true;
    }
} // namespace Desert::Editor::AssetFileOps
