#include "AssetEditorRegistry.hpp"

#include <Common/Core/Logger.hpp>

namespace Desert::Editor
{
    void AssetEditorRegistry::Register( Assets::AssetTypeID type, Factory factory )
    {
        if ( !factory )
        {
            LOG_ERROR( "[AssetEditorRegistry] refusing an empty factory for asset type '{}' — the type would "
                       "read as having an editor and open nothing.",
                       Assets::AssetTypeName( type ) );
            return;
        }

        // NAMED rather than overwritten quietly: the loser of a duplicate registration is decided by
        // startup order, so a reader of the log has to be told which editor actually won.
        if ( m_Factories.find( type ) != m_Factories.end() )
        {
            LOG_WARN( "[AssetEditorRegistry] asset type '{}' already had an editor — the later registration "
                      "replaces it.",
                      Assets::AssetTypeName( type ) );
        }

        m_Factories[type] = std::move( factory );
    }

    bool AssetEditorRegistry::HasEditorFor( Assets::AssetTypeID type ) const noexcept
    {
        return m_Factories.find( type ) != m_Factories.end();
    }

    std::unique_ptr<IAssetEditorPanel> AssetEditorRegistry::Create( const Assets::AssetHandle& subject,
                                                                    Assets::AssetTypeID        type ) const
    {
        if ( static_cast<uint64_t>( subject ) == 0 )
        {
            LOG_ERROR( "[AssetEditorRegistry] open request for asset type '{}' carries the null handle — "
                       "whatever resolved the file to an asset failed and said nothing.",
                       Assets::AssetTypeName( type ) );
            return nullptr;
        }

        const auto it = m_Factories.find( type );
        if ( it == m_Factories.end() )
        {
            LOG_WARN( "[AssetEditorRegistry] no editor is registered for asset type '{}' (handle {}) — nothing "
                      "opened.",
                      Assets::AssetTypeName( type ), static_cast<uint64_t>( subject ) );
            return nullptr;
        }

        auto document = it->second( subject );
        if ( !document )
        {
            LOG_ERROR( "[AssetEditorRegistry] the '{}' editor could not build a document for handle {}.",
                       Assets::AssetTypeName( type ), static_cast<uint64_t>( subject ) );
        }
        return document;
    }
} // namespace Desert::Editor
