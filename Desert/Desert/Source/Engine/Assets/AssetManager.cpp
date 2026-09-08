#include <Engine/Assets/AssetManager.hpp>

#include <algorithm>

namespace Desert::Assets
{
    namespace
    {
        // Function-local so it is constructed on first use whatever the static-initialisation order is —
        // an AssetManager can be built from another translation unit's static.
        std::vector<AssetManager*>& LiveManagerList()
        {
            static std::vector<AssetManager*> managers;
            return managers;
        }
    } // namespace

    const std::vector<AssetManager*>& AssetManager::LiveManagers()
    {
        return LiveManagerList();
    }

    AssetManager::AssetManager()
    {
        LiveManagerList().push_back( this );
    }

    AssetManager::~AssetManager()
    {
        auto& managers = LiveManagerList();
        managers.erase( std::remove( managers.begin(), managers.end(), this ), managers.end() );
    }

} // namespace Desert::Assets
