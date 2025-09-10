#pragma once

#include "IComponentWidget.hpp"

namespace Desert::Editor
{
    class StaticMeshComponentWidget final : public ComponentWidget<ECS::StaticMeshComponent>
    {
    public:
        StaticMeshComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager );

        bool CanRemove() const override
        {
            return false;
        }

        void Render( ECS::Entity& entity ) override;

    private:
        void RenderPrimitiveSection( ECS::StaticMeshComponent& staticMesh );
        void RenderAssetSection( ECS::StaticMeshComponent& staticMesh );

    private:
        const std::weak_ptr<Assets::AssetManager> m_AssetManager;
    };
} // namespace Desert::Editor