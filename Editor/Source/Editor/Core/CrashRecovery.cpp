#include "CrashRecovery.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Core/Logger.hpp>
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

    bool CrashRecovery::ArmSession()
    {
        std::error_code ec;
        std::filesystem::create_directories( AutosaveDir(), ec );
        if ( ec )
        {
            LOG_ERROR( "[Recovery] Could not create {}: {} — this session is UNPROTECTED: a crash will "
                       "not be detected on the next start.",
                       AutosaveDir().string(), ec.message() );
            return false;
        }

        // WasUncleanExit() is literally exists( LockPath() ), so a lock that failed to appear is
        // indistinguishable from a clean exit: the editor crashes, the next start sees no lock and
        // never offers the recovery. Saying so at arm time is the only moment the difference exists.
        if ( const auto written =
                  Common::Utils::FileSystem::WriteContentToFileAtomic( LockPath(), "editor session in progress" );
             !written )
        {
            LOG_ERROR( "[Recovery] Could not arm the session lock {}: {} — this session is UNPROTECTED: "
                       "a crash will not be detected on the next start.",
                       LockPath().string(), written.GetError() );
            return false;
        }
        return true;
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
