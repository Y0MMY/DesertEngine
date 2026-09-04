#include "SkinnedMeshAsset.hpp"
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <Engine/Assets/Serialization/Mesh.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    SkinnedMeshAsset::SkinnedMeshAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : MeshAsset( priority, filepath, GetTypeID() )
    {
        // The path-derived handle this type used to compute for itself (twice — here and again in Load)
        // now comes from AssetBase, which derives it the same way for every asset type. See the comment on
        // that constructor.
    }

    Common::BoolResultStr SkinnedMeshAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        // DefaultIfMissing: meshes cooked before a field existed (e.g. MorphTargets) still load.
        const auto dataReflected =
             rfl::json::read<Serialization::MeshAssetData, rfl::DefaultIfMissing>( raw );

        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        const auto& data = dataReflected.value();

        if ( !data.IsSkinned )
        {
            return Common::MakeError( "SkinnedMeshAsset cannot load static mesh data." );
        }

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

        m_MorphTargets.clear();
        m_MorphTargets.reserve( data.MorphTargets.size() );
        for ( const auto& mt : data.MorphTargets )
            m_MorphTargets.push_back( MorphTarget{ mt.Name, mt.DeltaPositions, mt.DeltaNormals } );

        // StaticMeshAsset::Load has always ended this way; this one never did, so IsReadyForUse stayed false
        // for the whole session and every `if (!IsReadyForUse()) Load()` in the engine re-read and re-parsed
        // the entire .skmesh. MeshService::GetAsset is on the per-frame path, so the cost was a full JSON
        // parse of the character PER FRAME, silently, on top of the mesh being invisible.
        m_IsReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr SkinnedMeshAsset::Unload()
    {
        m_Vertices.clear();
        m_Indices.clear();
        m_Submeshes.clear();
        m_MorphTargets.clear();

        m_Vertices.shrink_to_fit();
        m_Indices.shrink_to_fit();
        m_Submeshes.shrink_to_fit();
        m_MorphTargets.shrink_to_fit();

        // The flag is what EnsureLoaded asks before deciding to parse, so an emptied asset that still
        // reports "ready" is an asset nobody will ever reload — the same never-recovers shape as the
        // dependency this file just stopped losing.
        m_IsReadyForUse = false;
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets