#pragma once

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>

namespace Desert::Editor
{
    /*class MaterialsPanel
    {
    public:
        MaterialsPanel( const std::shared_ptr<Assets::AssetManager>& assetManager )
             : m_AssetManager( assetManager )
        {
        }

        void DrawMaterialEditor( std::shared_ptr<Graphic::MaterialInstance>& material );
        void DrawMaterialEntity( const ECS::Entity& entity, const Assets::Asset<Assets::MeshAsset>& meshAsset );
        void DrawMaterialInfo( std::shared_ptr<Graphic::MaterialInstance>& materialInstance );

        void CreateMaterialAsset( const ECS::Entity&                              selectedEntity,
                                  const std::shared_ptr<Assets::MeshAsset>& meshAsset );

    private:
        bool NeedCreateMaterialAsset( const ECS::MaterialComponent& materialComponent );

        void DrawTextureSlot( const char* label, Assets::TextureAsset::Type type,
                              std::shared_ptr<Graphic::MaterialInstance>& material );
        void DrawMaterialProperties( std::shared_ptr<Graphic::MaterialInstance>& material );

        const std::shared_ptr<Assets::AssetManager> m_AssetManager;
    };*/

} // namespace Desert::Editor