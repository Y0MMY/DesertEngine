#include "BuiltinMeshRegistry.hpp"

#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Editor
{
    std::unordered_map<BuiltinMeshType, Assets::AssetHandle> BuiltinMeshRegistry::s_BuiltinMeshes;
    std::unordered_map<BuiltinMeshType, std::vector<Assets::AssetHandle>> BuiltinMeshRegistry::s_BuiltinDefaultMaterials;

    void BuiltinMeshRegistry::Init( const std::shared_ptr<Assets::AssetManager>& assetManager )
    {
        RegisterCube( assetManager );
        RegisterPlane( assetManager );
        RegisterSphere( assetManager );
    }

    Assets::AssetHandle BuiltinMeshRegistry::Get( BuiltinMeshType type )
    {
        auto it = s_BuiltinMeshes.find( type );
        if ( it == s_BuiltinMeshes.end() )
            return {};

        return it->second;
    }

    std::vector<Assets::AssetHandle> BuiltinMeshRegistry::GetDefaultMaterials( BuiltinMeshType type )
    {
        auto it = s_BuiltinDefaultMaterials.find( type );
        if ( it == s_BuiltinDefaultMaterials.end() )
            return {};

        return it->second;
    }

    Assets::AssetHandle
    BuiltinMeshRegistry::CreateDefaultMaterial( const std::shared_ptr<Assets::AssetManager>& assetManager )
    {
        //auto material = std::make_shared<Graphic::Material>();
        //material->SetName( "Default Material" );
        //material->SetAlbedo( glm::vec3( 1.0f, 1.0f, 1.0f ) );
        //material->SetRoughness( 0.5f );
        //material->SetMetallic( 0.0f );

        //auto materialAsset = assetManager->CreateRuntimeAsset<Assets::MaterialAsset>( material );
        //auto handle        = materialAsset->GetMetadata().Handle;

        //Runtime::ResourceRegistry::GetMaterialService()->Register( materialAsset );
        // Stub: no material is actually created yet -> report "no material" (Null), not a random id.
        Assets::AssetHandle handle = Assets::AssetHandle::Null();

        return handle;
    }


    // =========================
    // CUBE
    // =========================
    void BuiltinMeshRegistry::RegisterCube( const std::shared_ptr<Assets::AssetManager>& assetManager )
    {
        auto cubeMesh = Geometry::PrimitiveMeshFactory::Create( Geometry::PrimitiveType::Cube );

        const auto handle = Runtime::ResourceRegistry::GetMeshService()->RegisterProcedural( cubeMesh );

        s_BuiltinMeshes[BuiltinMeshType::Cube] = handle;
    }

    // =========================
    // PLANE
    // =========================
    void BuiltinMeshRegistry::RegisterPlane( const std::shared_ptr<Assets::AssetManager>& assetManager )
    {
       /* auto data = Geometry::PrimitiveMeshFactory::CreatePlane();

        auto asset = assetManager->CreateRuntimeAsset<Assets::StaticMeshAsset>( data.vertices, data.indices,
                                                                                data.submeshes );

        auto handle = asset->GetMetadata().Handle;

        Runtime::ResourceRegistry::GetMeshService()->Register( asset );

        s_BuiltinMeshes[BuiltinMeshType::Plane] = handle;*/
    }

    // =========================
    // SPHERE
    // =========================
    void BuiltinMeshRegistry::RegisterSphere( const std::shared_ptr<Assets::AssetManager>& assetManager )
    {
        /*auto data = Geometry::PrimitiveMeshFactory::CreateSphere();

        auto asset = assetManager->CreateRuntimeAsset<Assets::StaticMeshAsset>( data.vertices, data.indices,
                                                                                data.submeshes );

        auto handle = asset->GetMetadata().Handle;

        Runtime::ResourceRegistry::GetMeshService()->Register( asset );

        s_BuiltinMeshes[BuiltinMeshType::Sphere] = handle;*/
    }
} // namespace Desert::Editor