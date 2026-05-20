#pragma once

#include "IComponentWidget.hpp"

#include <Editor/Core/PrimitiveType.hpp>

namespace Desert::Editor
{
    class StaticMeshComponentWidget final : public ComponentWidget<ECS::StaticMeshComponent>
    {
    public:
        StaticMeshComponentWidget( const Assets::AssetManager* assetManager );

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity ) override;

    private:
        void RenderPrimitiveSection( ECS::StaticMeshComponent& staticMesh );
        void RenderAssetSection( ECS::StaticMeshComponent& staticMesh );

    private:
        void        SetMeshAsset( ECS::StaticMeshComponent& staticMesh, const Assets::AssetHandle& handle );
        void        SetPrimitive( ECS::StaticMeshComponent& staticMesh, PrimitiveType type );
        bool        IsPrimitiveSelected( const ECS::StaticMeshComponent& staticMesh, PrimitiveType type ) const;
        std::string GetPrimitiveName( const ECS::StaticMeshComponent& staticMesh ) const;
        void        RenderPrimitiveInfo( ECS::StaticMeshComponent& staticMesh );

    private:
        const Assets::AssetManager* m_AssetManager;
    };
} // namespace Desert::Editor