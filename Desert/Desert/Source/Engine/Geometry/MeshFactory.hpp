#pragma once

#include "Mesh.hpp"

namespace Desert::Graphic
{
    class MeshFactory
    {
    public:
        enum class MeshType
        {
            Auto,
            Static,
            Skinned
        };

        static std::shared_ptr<Mesh> Create( const std::shared_ptr<Assets::MeshAsset>& baseAsset,
                                             MeshType                                  type = MeshType::Auto )
        {
            std::shared_ptr<Mesh> mesh;

            switch ( type )
            {
                case MeshType::Static:
                    mesh = Mesh::CreateStatic( baseAsset );
                    break;

                case MeshType::Skinned:
                    mesh = Mesh::CreateSkinned( baseAsset );
                    break;

                case MeshType::Auto:
                default:
                    mesh = CreateAuto( baseAsset );
                    break;
            }

            auto result = mesh->Invalidate();
            if ( !result.IsSuccess() )
            {
                LOG_ERROR( "Failed to invalidate mesh: {}", result.GetError() );
                return nullptr;
            }

            return mesh;
        }

    private:
        static std::shared_ptr<Mesh> CreateAuto( const std::shared_ptr<Assets::MeshAsset>& baseAsset )
        {
            const bool isSkinned = Mesh::DetectIfSkinned( baseAsset );

            if ( isSkinned )
            {
                return Mesh::CreateSkinned( baseAsset );
            }
            else
            {
                return Mesh::CreateStatic( baseAsset );
            }
        }
    };
} // namespace Desert::Graphic