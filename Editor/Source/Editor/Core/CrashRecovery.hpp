#pragma once

#include <filesystem>

namespace Desert::Editor
{
    // Crash recovery for the scene editor. A lock file lives in the autosave folder while the editor
    // runs; a clean shutdown removes it. If the lock is still there at the next start, the previous
    // session did not exit cleanly — and if an autosave exists, the editor offers to reopen it.
    //
    // This pairs with the existing autosave timer (EditorLayer + EditorPreferences::AutosaveMinutes),
    // which periodically writes Scene/Autosave/<name>_autosave.desce; recovery just reopens the newest
    // one through the normal scene-load path. Project-scoped via Constants::Path::SCENE_PATH.
    class CrashRecovery
    {
    public:
        // Autosave directory and the session lock inside it.
        static std::filesystem::path AutosaveDir();
        static std::filesystem::path LockPath();

        // Call ONCE at startup, before ArmSession(): true if the last session left the lock behind.
        static bool WasUncleanExit();

        // Create/refresh the session lock ("editor running").
        static void ArmSession();

        // Remove the session lock (clean shutdown).
        static void DisarmSession();

        // Newest *_autosave.desce in the autosave dir, or empty when there is none.
        static std::filesystem::path LatestAutosave();
    };
} // namespace Desert::Editor
