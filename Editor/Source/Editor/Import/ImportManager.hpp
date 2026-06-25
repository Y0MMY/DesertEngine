#pragma once

#include <memory>
#include <unordered_map>
#include "IAssetImporter.hpp"
#include "TextureImporter.hpp"

#include <Engine/Assets/AssetManager.hpp>

namespace Desert::Editor
{
    class ImportManager
    {
    public:
        ImportManager();

        // force = re-cook even if an up-to-date cooked output already exists (Rebuild Cooked Assets).
        void         Import( const std::filesystem::path& path, bool force = false );
        void         ImportAllFromDirectory( const std::filesystem::path& root, bool force = false );
        Common::UUID ImportTexture( const std::filesystem::path& path );

        // Cook a source texture into Cooked/Textures/*.tex, create+register a TextureAsset, and return its
        // handle (the same handle TextureService keys by). Returns a zero handle on failure. Drives the
        // import-on-demand drag-drop path (see Editor::TextureDnD).
        Assets::AssetHandle ImportAndRegisterTexture( Assets::AssetManager&         mgr,
                                                      const std::filesystem::path& source );

    private:
        void CreateAssetsFromImport( const ImportResult& result, const std::filesystem::path& sourcePath );

    private:
        void SerializeMeshAsset( const Desert::Assets::Serialization::MeshAssetData& data,
                                 const std::filesystem::path&                        sourcePath );

        void SerializeMaterialAsset( const Desert::Assets::Serialization::MaterialAssetData& data,
                                     const std::filesystem::path&                            sourcePath );

        void SerializeSkeletonAsset( const Desert::Assets::Serialization::SkeletonAssetData& data,
                                     const std::filesystem::path&                            sourcePath );

        void SerializeAnimationAsset( const Desert::Assets::Serialization::AnimationAssetData& data,
                                      const std::filesystem::path&                             sourcePath );

    private:
        std::unordered_map<std::string, std::unique_ptr<IAssetImporter>> m_Importers;
        std::unique_ptr<TextureImporter>                                 m_TextureImporter;
    };
} // namespace Desert::Editor