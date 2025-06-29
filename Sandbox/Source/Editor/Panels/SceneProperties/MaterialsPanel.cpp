#include "MaterialsPanel.hpp"
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <ImGui/imgui.h>
#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    //void MaterialsPanel::DrawMaterialEditor( std::shared_ptr<Graphic::MaterialInstance>& material )
    //{
    //    if ( !material )
    //        return;

    //    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8, 8 ) );

    //    // Draw texture slots
    //    DrawTextureSlot( "Albedo", Assets::TextureAsset::Type::Albedo, material );
    //    DrawTextureSlot( "Metallic", Assets::TextureAsset::Type::Metallic, material );
    //    DrawTextureSlot( "Roughness", Assets::TextureAsset::Type::Roughness, material );
    //    DrawTextureSlot( "Normal", Assets::TextureAsset::Type::Normal, material );
    //    DrawTextureSlot( "AO", Assets::TextureAsset::Type::AO, material );
    //    DrawTextureSlot( "Emissive", Assets::TextureAsset::Type::Emissive, material );

    //    ImGui::Separator();
    //    ImGui::Dummy( ImVec2( 0, 5 ) );

    //    // Draw material properties
    //    DrawMaterialProperties( material );

    //    ImGui::PopStyleVar();
    //}

    //void MaterialsPanel::DrawMaterialEntity( const ECS::Entity&                      entity,
    //                                         const Assets::Asset<Assets::MeshAsset>& meshAsset )
    //{
    //    if ( !entity.HasComponent<ECS::MaterialComponent>() )
    //        return;

    //    auto& materialComponent = entity.GetComponent<ECS::MaterialComponent>();
    //    CreateMaterialAsset( entity, meshAsset );

    //    if ( ImGui::CollapsingHeader( "Material", ImGuiTreeNodeFlags_DefaultOpen ) )
    //    {
    //        if ( !materialComponent.MaterialInstance )
    //            return;

    //        ImGui::Dummy( ImVec2( 0, 4 ) );
    //        DrawMaterialInfo( materialComponent.MaterialInstance );
    //    }
    //}

    //void MaterialsPanel::DrawMaterialInfo( std::shared_ptr<Graphic::MaterialInstance>& materialInstance )
    //{
    //    if ( !materialInstance )
    //        return;

    //    // Display base material info if exists
    //    if ( materialInstance->IsUsingBaseMaterial() )
    //    {
    //        const auto& baseMaterial = materialInstance->GetBaseMaterial();
    //        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.8f, 0.8f, 0.85f, 1.0f ) );
    //        ImGui::Text( "Base Material:" );
    //        ImGui::PopStyleColor();

    //        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.7f, 0.7f, 0.75f, 1.0f ) );
    //        ImGui::TextWrapped( "%s", baseMaterial->GetMaterialAssetPath().string().c_str() );
    //        ImGui::PopStyleColor();

    //        ImGui::Dummy( ImVec2( 0, 10 ) );
    //    }

    //    DrawMaterialEditor( materialInstance );
    //}

    //bool MaterialsPanel::NeedCreateMaterialAsset( const ECS::MaterialComponent& materialComponent )
    //{
    //    if ( !materialComponent.MaterialInstance )
    //        return false;
    //    return true;
    //}

    //void MaterialsPanel::CreateMaterialAsset( const ECS::Entity&                        selectedEntity,
    //                                          const std::shared_ptr<Assets::MeshAsset>& meshAsset )
    //{
    //    if ( !selectedEntity.HasComponent<ECS::MaterialComponent>() )
    //    {
    //        return;
    //    }

    //    auto& materialComponent = selectedEntity.GetComponent<ECS::MaterialComponent>();
    //    if ( !NeedCreateMaterialAsset( materialComponent ) )
    //        return;

    //    // Create a new material instance if none exists
    //    if ( !materialComponent.MaterialInstance )
    //    {
    //        materialComponent.MaterialInstance =
    //             Graphic::MaterialFactory::Create( m_AssetManager, meshAsset->GetBaseFilepath() );
    //    }
    //}

    //void MaterialsPanel::DrawTextureSlot( const char* label, Assets::TextureAsset::Type type,
    //                                      std::shared_ptr<Graphic::MaterialInstance>& material )
    //{
    //    if ( !material )
    //        return;

    //    ImGui::PushID( label );

    //    ImGui::Text( "%s:", label );

    //    std::string path       = "None";
    //    bool        hasTexture = material->HasFinalTexture( type );

    //    if ( hasTexture )
    //    {
    //        const auto& texture = material->GetFinalTexture( type );
    //        path = material->GetTexture( type ) ? material->GetTexture( type )->GetBaseFilepath().string()
    //                                            : "From Base Material";

    //        ImGui::SameLine();
    //        ImGui::TextColored( ImVec4( 0.8f, 0.8f, 0.8f, 1.0f ), "%s (%dx%d)", path.c_str(), texture->GetWidth(),
    //                            texture->GetHeight() );
    //    }
    //    else
    //    {
    //        ImGui::SameLine();
    //        ImGui::TextColored( ImVec4( 0.5f, 0.5f, 0.5f, 1.0f ), "%s", path.c_str() );
    //    }

    //    // Remove texture button if there's an override
    //    if ( material->HasTexture( type ) )
    //    {
    //        ImGui::SameLine();
    //        if ( ImGui::Button( "Remove", ImVec2( 80, 0 ) ) )
    //        {
    //            material->RemoveTexture( type );
    //        }
    //    }

    //    ImGui::PopID();
    //}

    //void MaterialsPanel::DrawMaterialProperties( std::shared_ptr<Graphic::MaterialInstance>& material )
    //{
    //    if ( !material )
    //        return;

    //    // Albedo color control
    //    /* if ( ImGui::ColorEdit3( "Albedo Color", &material->GetAlbedoColor()[0] ) )
    //     {
    //         material->UpdateRenderParameters();
    //     }*/

    //    // Albedo blend control
    //    float albedoBlend = material->GetAlbedoBlend();
    //    if ( ImGui::SliderFloat( "Albedo Blend", &albedoBlend, 0.0f, 1.0f ) )
    //    {
    //        material->SetAlbedo( material->GetAlbedoColor(), albedoBlend );
    //    }

    //    ImGui::Separator();

    //    // Metallic control
    //    float metallicValue = material->GetMetallicValue();
    //    if ( ImGui::SliderFloat( "Metallic", &metallicValue, 0.0f, 1.0f ) )
    //    {
    //        material->SetMetallic( metallicValue, material->GetMetallicBlend() );
    //    }

    //    // Metallic blend control
    //    float metallicBlend = material->GetMetallicBlend();
    //    if ( ImGui::SliderFloat( "Metallic Blend", &metallicBlend, 0.0f, 1.0f ) )
    //    {
    //        material->SetMetallic( material->GetMetallicValue(), metallicBlend );
    //    }

    //    ImGui::Separator();

    //    // Roughness control
    //    float roughnessValue = material->GetRoughnessValue();
    //    if ( ImGui::SliderFloat( "Roughness", &roughnessValue, 0.0f, 1.0f ) )
    //    {
    //        material->SetRoughness( roughnessValue, material->GetRoughnessBlend() );
    //    }

    //    // Roughness blend control
    //    float roughnessBlend = material->GetRoughnessBlend();
    //    if ( ImGui::SliderFloat( "Roughness Blend", &roughnessBlend, 0.0f, 1.0f ) )
    //    {
    //        material->SetRoughness( material->GetRoughnessValue(), roughnessBlend );
    //    }

    //    ImGui::Separator();

    //    // Emission controls
    //    glm::vec3 emissionColor = material->GetEmissionColor();
    //    if ( ImGui::ColorEdit3( "Emission Color", &emissionColor[0] ) )
    //    {
    //        material->SetEmission( emissionColor, material->GetEmissionStrength() );
    //    }

    //    float emissionStrength = material->GetEmissionStrength();
    //    if ( ImGui::SliderFloat( "Emission Strength", &emissionStrength, 0.0f, 10.0f ) )
    //    {
    //        material->SetEmission( material->GetEmissionColor(), emissionStrength );
    //    }

    //    ImGui::Separator();

    //    // AO control
    //    float aoValue = material->GetAOValue();
    //    if ( ImGui::SliderFloat( "Ambient Occlusion", &aoValue, 0.0f, 1.0f ) )
    //    {
    //        material->SetAO( aoValue );
    //    }
    //}
} // namespace Desert::Editor