#pragma once

#include <memory>
#include <unordered_map>
#include "IAssetImporter.hpp"

namespace Desert::Editor
{
    class ImportManager
    {
    public:
        ImportManager();

        void Import( const std::filesystem::path& path );
        void ImportAllFromDirectory( const std::filesystem::path& root );

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
    };
} // namespace Desert::Editor