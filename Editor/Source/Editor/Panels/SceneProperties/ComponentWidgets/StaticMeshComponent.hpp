#pragma once

#include "IComponentWidget.hpp"

#include <Engine/Geometry/PrimitiveType.hpp>

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

        void Render( ECS::Entity& entity, ::Desert::Core::Scene* scene = nullptr ) override;

    private:
        void        SetMeshAsset( ECS::StaticMeshComponent& staticMesh, const Assets::AssetHandle& handle );
        std::string GetPrimitiveName( const ECS::StaticMeshComponent& staticMesh ) const;

        // In-editor rigging section: place bones on an asset-backed static mesh and "Convert to Skinned".
        void RenderRigging( ECS::Entity& entity, ECS::StaticMeshComponent& staticMesh );

    private:
        const Assets::AssetManager* m_AssetManager;
    };
} // namespace Desert::Editor