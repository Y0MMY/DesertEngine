#include "SkinnedMeshAsset.hpp"
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <Engine/Assets/Serialization/Mesh.hpp>
#include <Engine/Assets/AssetId.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    SkinnedMeshAsset::SkinnedMeshAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : MeshAsset( priority, filepath, GetTypeID() )
    {
        // Path-derived handle in the ctor (see StaticMeshAsset) so a not-yet-loaded registry shell is keyed
        // correctly. Load re-derives the same value.
        m_Metadata.Handle = StableAssetId( m_Metadata.Filepath.lexically_normal().generic_string() );
    }

    Common::BoolResultStr SkinnedMeshAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto dataReflected = rfl::json::read<Serialization::MeshAssetData>( raw );

        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        const auto& data = dataReflected.value();

        if ( !data.IsSkinned )
        {
            return Common::MakeError( "SkinnedMeshAsset cannot load static mesh data." );
        }

        // Stable, path-derived handle (see StaticMeshAsset::Load) — survives re-cooks + lets the registry
        // compute it from the path without parsing the mesh payload.
        m_Metadata.Handle = StableAssetId( m_Metadata.Filepath.lexically_normal().generic_string() );

        m_Vertices.clear();
        m_Indices.clear();
        m_Submeshes.clear();

        m_Vertices.reserve( data.SkinnedVertices.size() );
        m_Indices.reserve( data.Indices.size() );
        m_Submeshes.reserve( data.Submeshes.size() );

        for ( const auto& v : data.SkinnedVertices )
        {
            SkinnedVertex vertex;

            vertex.StaticVertex.Position  = v.Position;
            vertex.StaticVertex.Normal    = v.Normal;
            vertex.StaticVertex.Tangent   = v.Tangent;
            vertex.StaticVertex.Bitangent = v.Bitangent;
            vertex.StaticVertex.TexCoord  = v.TexCoord;

            vertex.BoneIDs     = v.BoneIDs;
            vertex.BoneWeights = v.BoneWeights;

            m_Vertices.emplace_back( std::move( vertex ) );
        }

        for ( const auto& i : data.Indices )
        {
            Index index;
            index.V1 = i.V1;
            index.V2 = i.V2;
            index.V3 = i.V3;

            m_Indices.emplace_back( index );
        }

        for ( const auto& s : data.Submeshes )
        {
            Submesh submesh;
            submesh.Name         = s.Name;
            submesh.VertexOffset = s.VertexOffset;
            submesh.VertexCount  = s.VertexCount;
            submesh.IndexOffset  = s.IndexOffset;
            submesh.IndexCount   = s.IndexCount;
            submesh.Transform    = s.Transform;
            submesh.BoundingBox  = s.BoundingBox;

            m_Submeshes.emplace_back( std::move( submesh ) );
        }

        if ( !data.SkeletonSignature.has_value() )
        {
            return Common::MakeError( "SkinnedMeshAsset requires SkeletonUUID." );
        }

        m_SkeletonSignature = data.SkeletonSignature.value();

        return BOOLSUCCESS;
    }

    Common::BoolResultStr SkinnedMeshAsset::Unload()
    {
        m_Vertices.clear();
        m_Indices.clear();
        m_Submeshes.clear();

        m_Vertices.shrink_to_fit();
        m_Indices.shrink_to_fit();
        m_Submeshes.shrink_to_fit();

        return BOOLSUCCESS;
    }
} // namespace Desert::Assets