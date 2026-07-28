#include "CrashRecovery.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Editor
{
    std::filesystem::path CrashRecovery::AutosaveDir()
    {
        return Common::Constants::Path::SCENE_PATH / "Autosave";
    }

    std::filesystem::path CrashRecovery::LockPath()
    {
        return AutosaveDir() / ".session.lock";
    }

    bool CrashRecovery::WasUncleanExit()
    {
        std::error_code ec;
        return std::filesystem::exists( LockPath(), ec );
    }

    void CrashRecovery::ArmSession()
    {
        std::error_code ec;
        std::filesystem::create_directories( AutosaveDir(), ec );
        Common::Utils::FileSystem::WriteContentToFile( LockPath(), "editor session in progress" );
    }

    void CrashRecovery::DisarmSession()
    {
        std::error_code ec;
        std::filesystem::remove( LockPath(), ec );
    }

    std::filesystem::path CrashRecovery::LatestAutosave()
    {
        namespace fs = std::filesystem;
        const fs::path              dir = AutosaveDir();
        fs::path                    newest;
        fs::file_time_type          newestTime{};
        std::error_code             ec;

        for ( const auto& entry : fs::directory_iterator( dir, ec ) )
        {
            if ( ec )
                break;
            const fs::path& p = entry.path();
            if ( p.extension() != Common::Constants::Extensions::SCENE_EXTENSION )
                continue;
            if ( p.filename().string().find( "_autosave" ) == std::string::npos )
                continue;

            const auto t = fs::last_write_time( p, ec );
            if ( ec )
                continue;
            if ( newest.empty() || t > newestTime )
            {
                newest     = p;
                newestTime = t;
            }
        }
        return newest;
    }
} // namespace Desert::Editor
