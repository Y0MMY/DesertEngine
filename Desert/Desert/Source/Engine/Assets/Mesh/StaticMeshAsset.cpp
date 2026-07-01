#include "StaticMeshAsset.hpp"

#include <Common/Core/Serialization/GlmReflection.hpp>
#include <Engine/Assets/Serialization/Mesh.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    StaticMeshAsset::StaticMeshAsset( const AssetPriority priority, const Common::Filepath& filepath )
         : MeshAsset( priority, filepath, GetTypeID() )
    {
        // Path-derived handle in the ctor (not just Load) so a NOT-yet-loaded registry shell is keyed by the
        // correct stable handle. Load re-derives the same value.
        m_Metadata.Handle = Common::AssetHandle::FromCookedPath( m_Metadata.Filepath );
    }

    Common::BoolResultStr StaticMeshAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto dataReflected = rfl::json::read<Serialization::MeshAssetData>( raw );

        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        const auto& data = dataReflected.value();

        // Safety check
        if ( data.IsSkinned )
        {
            return Common::MakeError( "StaticMeshAsset cannot load skinned mesh data." );
        }

        // Stable, path-derived handle: the .stmesh has no stored handle (unlike .tex/.demat), so the mesh used
        // to get a RANDOM per-session id. Derive it deterministically from the cooked path so it survives
        // re-cooks/restarts (saved scenes resolve the mesh) AND the asset registry can compute the same handle
        // from the path without parsing the (large) .stmesh. Normalized so every call site agrees.
        m_Metadata.Handle = Common::AssetHandle::FromCookedPath( m_Metadata.Filepath );

        m_Vertices.clear();
        m_Indices.clear();
        m_Submeshes.clear();

        m_Vertices.reserve( data.StaticVertices.size() );
        m_Indices.reserve( data.Indices.size() );
        m_Submeshes.reserve( data.Submeshes.size() );
        m_MaterialAssetHandles.reserve( data.Submeshes.size() );

        for ( const auto& v : data.StaticVertices )
        {
            Vertex vertex;
            vertex.Position  = v.Position;
            vertex.Normal    = v.Normal;
            vertex.Tangent   = v.Tangent;
            vertex.Bitangent = v.Bitangent;
            vertex.TexCoord  = v.TexCoord;

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

            m_MaterialAssetHandles.emplace_back( s.MaterialHandle );
            m_Submeshes.emplace_back( std::move( submesh ) );
        }

        m_IsReadyForUse = true;
        return BOOLSUCCESS;
    }

    Common::BoolResultStr StaticMeshAsset::Unload()
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