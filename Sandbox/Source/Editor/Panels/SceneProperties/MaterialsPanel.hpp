#pragma once

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>

namespace Desert::Editor
{
    class MaterialsPanel
    {
    public:
        MaterialsPanel( const std::shared_ptr<Assets::AssetManager>& assetManager )
             : m_AssetManager( assetManager )
        {
        }

        void DrawMaterialEditor( Assets::Asset<Assets::MaterialAsset>& material );
        void DrawMaterialEntity( const ECS::Entity& entity );
        void DrawMaterialInfo( std::shared_ptr<Graphic::MaterialInstance>& materialInstance );

    private:
        bool NeedCreateMaterialAsset( const ECS::MaterialComponent& materialComponent );
        void CreateMaterialAsset( ECS::MaterialComponent&                   materialComponent,
                                  const std::shared_ptr<Assets::MeshAsset>& meshAsset );

        void DrawTextureSlot( const char* label, Assets::TextureAsset::Type type,
                              Assets::Asset<Assets::MaterialAsset>& material );
        void DrawMaterialProperties( Assets::Asset<Assets::MaterialAsset>& material );

        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
    };

} // namespace Desert::Editor