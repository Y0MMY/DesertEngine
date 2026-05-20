#pragma once

#include <memory>
#include <unordered_map>
#include "IAssetImporter.hpp"
#include "TextureImporter.hpp"

namespace Desert::Editor
{
    class ImportManager
    {
    public:
        ImportManager();

        void         Import( const std::filesystem::path& path );
        void         ImportAllFromDirectory( const std::filesystem::path& root );
        Common::UUID ImportTexture( const std::filesystem::path& path );

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