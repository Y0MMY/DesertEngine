#pragma once

#include <Common/Core/Timestep.hpp>

#include <filesystem>
#include <memory>
#include <unordered_map>

namespace Desert::Assets
{
    class AssetManager;
}
namespace Desert::Core
{
    class Scene;
}

namespace Desert::Runtime
{
    // Asset hot-reload: polls the mtimes of editable asset files and pushes changes into the
    // running engine without a restart.
    //
    //   .demat  — re-parses the material asset and re-applies it onto its runtime material
    //             (PBR fast path or DataDrivenMaterial); a SHADER change in the file rebuilds
    //             the runtime material and refreshes every mesh component using that slot.
    //   .shader — recompiles the program (errors land in the log / Logs panel, the old
    //             pipelines keep drawing); on success the pipeline cache entries for that
    //             shader are dropped after a device-idle wait, so the next frame draws with
    //             the new code. Renderer-owned specialized pipelines (batched PBR, shadows)
    //             still need a restart — logged when they're affected.
    //
    // Polling (not FS events) keeps it portable (macOS/Windows/Linux) and cheap: one stat()
    // per watched file per interval.
    class AssetHotReload
    {
    public:
        // Call once per frame. Cheap between poll intervals (just accumulates time).
        void Tick( const Common::Timestep& ts, Assets::AssetManager& assetManager, Core::Scene* scene );

    private:
        void PollMaterials( Assets::AssetManager& assetManager, Core::Scene* scene );
        void PollShaders( Assets::AssetManager& assetManager, Core::Scene* scene );

        // Records @p path's mtime and reports whether it MOVED since the last poll. A file seen for the
        // first time returns false: the first sighting is a baseline, not an edit.
        bool TouchWatched( const std::filesystem::path& path );

        using Clock = std::filesystem::file_time_type;
        std::unordered_map<std::string, Clock> m_KnownTimes;
        float                                  m_Accum       = 0.0f;
        bool                                   m_FirstScan   = true;
        static constexpr float                 kPollInterval = 0.7f; // seconds
    };
} // namespace Desert::Runtime
