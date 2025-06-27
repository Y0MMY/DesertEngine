#include "MaterialsPanel.hpp"

namespace Desert::Editor
{

    void MaterialsPanel::DrawMaterialEditor( Assets::Asset<Assets::MaterialAsset>& material )
    {
    }

    void MaterialsPanel::DrawMaterialEntity( const ECS::Entity& entity )
    {

    }

    void MaterialsPanel::DrawMaterialInfo( std::shared_ptr<Graphic::MaterialInstance>& materialInstance )
    {
    }

    bool MaterialsPanel::NeedCreateMaterialAsset( const ECS::MaterialComponent& materialComponent )
    {
    }

    void MaterialsPanel::CreateMaterialAsset( ECS::MaterialComponent&                   materialComponent,
                                              const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    {
    }

    void MaterialsPanel::DrawTextureSlot( const char* label, Assets::TextureAsset::Type type,
                                          Assets::Asset<Assets::MaterialAsset>& material )
    {
    }

    void MaterialsPanel::DrawMaterialProperties( Assets::Asset<Assets::MaterialAsset>& material )
    {
    }

} // namespace Desert::Editor