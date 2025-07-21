#include "MaterialsPanel.hpp"
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <ImGui/imgui.h>
#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    // =========================================================================
    // Main Material Editor Function
    // =========================================================================
    void MaterialsPanel::DrawMaterialEditor( const std::shared_ptr<Graphic::MaterialPBR>& material )
    {
        if ( !material )
            return;

        // Setup editor styling
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8, 6 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 4, 3 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 3.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 10, 8 ) );

        // Draw editor sections
        DrawMaterialInfo( material );
        ImGui::Separator();
        DrawMaterialProperties( material );

        // Restore original style
        ImGui::PopStyleVar( 4 );
    }

    // =========================================================================
    // Material Editor for Entity
    // =========================================================================
    void MaterialsPanel::DrawMaterialEntity( const ECS::Entity& entity )
    {
        // Only proceed if entity has a mesh component
        if ( !entity.HasComponent<ECS::StaticMeshComponent>() )
            return;

        // Get mesh components and validate
        auto& meshComponent = entity.GetComponent<ECS::StaticMeshComponent>();
        auto* meshInstance =
             m_ResourceManager->GetGeometryResources()->GetMeshCache().Get( meshComponent.MeshHandle );
        if ( !meshInstance || !meshInstance->IsReady() )
            return;

        // Get mesh asset
        auto meshAsset = meshInstance->GetMeshAsset();
        if ( !meshAsset )
            return;

        // Validate material handle
        auto materialHandle = meshComponent.MaterialHandle;
        if ( !materialHandle.IsValid() )
            return;

        // Get material instance
        auto material = m_ResourceManager->GetGeometryResources()->GetMaterialCache().Get( materialHandle );
        if ( !material )
            return;

        // Draw collapsible material panel
        if ( ImGui::CollapsingHeader( "Material", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Dummy( ImVec2( 0, 4 ) );
            DrawMaterialEditor( material );
        }
    }

    // =========================================================================
    // Material Information Section
    // =========================================================================
    void MaterialsPanel::DrawMaterialInfo( const std::shared_ptr<Graphic::MaterialPBR>& material )
    {
        // Section header styling
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.8f, 0.8f, 0.85f, 1.0f ) );
        ImGui::Text( "Material Info" );
        ImGui::PopStyleColor();
        ImGui::Dummy( ImVec2( 0, 4 ) );

        // Display base material information
        if ( material->IsUsingBaseMaterial() )
        {
            auto baseMaterial = material->GetBaseMaterial();
            ImGui::Text( "Base Material: %s", "Name" );
        }
        else
        {
            ImGui::Text( "Base Material: None (Custom)" );
        }

        // Draw texture slots with spacing
        ImGui::Dummy( ImVec2( 0, 6 ) );
        DrawTextureSlot( "Albedo", Assets::TextureAsset::Type::Albedo, material );
        DrawTextureSlot( "Normal", Assets::TextureAsset::Type::Normal, material );
        DrawTextureSlot( "Metallic", Assets::TextureAsset::Type::Metallic, material );
        DrawTextureSlot( "Roughness", Assets::TextureAsset::Type::Roughness, material );
        DrawTextureSlot( "AO", Assets::TextureAsset::Type::AO, material );
    }

    // =========================================================================
    // Texture Slot UI Element
    // =========================================================================
    void MaterialsPanel::DrawTextureSlot( const char* label, Assets::TextureAsset::Type type,
                                          const std::shared_ptr<Graphic::MaterialPBR>& material )
    {
        bool hasTexture = material->HasFinalTexture( type );
        ImGui::PushID( label );

        // Two-column layout for label and button
        ImGui::Columns( 2, nullptr, false );
        ImGui::SetColumnWidth( 0, 100.0f );

        // Texture slot label
        ImGui::Text( "%s", label );
        ImGui::NextColumn();

        // Button logic based on texture presence
        if ( hasTexture )
        {
            // Styling for remove button
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.3f, 0.3f, 0.3f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.4f, 0.4f, 0.4f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.25f, 0.25f, 0.25f, 1.0f ) );

            // Remove texture button
            if ( ImGui::Button( "Remove", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            {
                material->RemoveTexture( type );
            }

            // Texture preview tooltip on hover
            if ( ImGui::IsItemHovered() )
            {
                ImGui::BeginTooltip();
                ImGui::Text( "Preview:" );
                auto texture = material->GetFinalTexture( type );
                if ( texture )
                {
                    float previewSize = 128.0f;
                    m_UIHelper->Image( texture->GetImage2D(), ImVec2( previewSize, previewSize ), ImVec2( 0, 1 ),
                                       ImVec2( 1, 0 ) );
                }
                ImGui::EndTooltip();
            }

            ImGui::PopStyleColor( 3 );
        }
        else
        {
            // Styling for add button
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.2f, 0.2f, 0.2f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.3f, 0.3f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.15f, 0.15f, 0.15f, 1.0f ) );

            // Add texture button with file dialog
            if ( ImGui::Button( "Add", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            {
                auto path = Common::Utils::FileSystem::OpenFileDialog(
                     "Image Files (*.png;*.jpg;*.jpeg;*.tga;*.bmp)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All Files "
                     "(*.*)\0*.*\0" );
                if ( !path.empty() )
                {
                    // TODO: Load and assign texture
                }
            }

            ImGui::PopStyleColor( 3 );
        }

        // Restore single-column layout
        ImGui::Columns( 1 );
        ImGui::PopID();
    }

    // =========================================================================
    // Material Properties Section with Tabs
    // =========================================================================
    void MaterialsPanel::DrawMaterialProperties( const std::shared_ptr<Graphic::MaterialPBR>& material )
    {
        // Section header styling
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.8f, 0.8f, 0.85f, 1.0f ) );
        ImGui::Text( "Material Properties" );
        ImGui::PopStyleColor();
        ImGui::Dummy( ImVec2( 0, 4 ) );

        // Create tab bar for material properties
        if ( ImGui::BeginTabBar( "MaterialPropertiesTabs" ) )
        {
            // -----------------------------------------------------------------
            // Albedo Tab
            // -----------------------------------------------------------------
            if ( ImGui::BeginTabItem( "Albedo" ) )
            {
                ImGui::Dummy( ImVec2( 0, 4 ) );
                auto albedoColor = material->GetAlbedoColor();

                // Color picker styling
                ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );

                // Albedo color editor
                if ( ImGui::ColorEdit3( "Color", &albedoColor[0] ) )
                {
                    material->SetAlbedo( albedoColor, material->GetAlbedoBlend() );
                }
                ImGui::PopStyleColor( 3 );

                // Albedo blend slider
                float albedoBlend = material->GetAlbedoBlend();
                if ( ImGui::SliderFloat( "Blend", &albedoBlend, 0.0f, 1.0f, "%.2f" ) )
                {
                    material->SetAlbedo( material->GetAlbedoColor(), albedoBlend );
                }

                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // Metallic/Roughness Tab
            // -----------------------------------------------------------------
            if ( ImGui::BeginTabItem( "Metallic/Roughness" ) )
            {
                ImGui::Dummy( ImVec2( 0, 4 ) );

                // Metallic controls
                float metallic = material->GetMetallicValue();
                if ( ImGui::SliderFloat( "Metallic", &metallic, 0.0f, 1.0f, "%.2f" ) )
                {
                    material->SetMetallic( metallic, material->GetMetallicBlend() );
                }

                float metallicBlend = material->GetMetallicBlend();
                if ( ImGui::SliderFloat( "Metallic Blend", &metallicBlend, 0.0f, 1.0f, "%.2f" ) )
                {
                    material->SetMetallic( material->GetMetallicValue(), metallicBlend );
                }

                ImGui::Dummy( ImVec2( 0, 6 ) );

                // Roughness controls
                float roughness = material->GetRoughnessValue();
                if ( ImGui::SliderFloat( "Roughness", &roughness, 0.0f, 1.0f, "%.2f" ) )
                {
                    material->SetRoughness( roughness, material->GetRoughnessBlend() );
                }

                float roughnessBlend = material->GetRoughnessBlend();
                if ( ImGui::SliderFloat( "Roughness Blend", &roughnessBlend, 0.0f, 1.0f, "%.2f" ) )
                {
                    material->SetRoughness( material->GetRoughnessValue(), roughnessBlend );
                }

                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // Emission Tab
            // -----------------------------------------------------------------
            if ( ImGui::BeginTabItem( "Emission" ) )
            {
                ImGui::Dummy( ImVec2( 0, 4 ) );
                auto emissionColor = material->GetEmissionColor();

                // Color picker styling
                ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.11f, 0.11f, 0.12f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.13f, 0.13f, 0.14f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.09f, 0.09f, 0.10f, 1.0f ) );

                // Emission color editor
                if ( ImGui::ColorEdit3( "Color", &emissionColor[0] ) )
                {
                    material->SetEmission( emissionColor, material->GetEmissionStrength() );
                }
                ImGui::PopStyleColor( 3 );

                // Emission strength slider
                float emissionStrength = material->GetEmissionStrength();
                if ( ImGui::SliderFloat( "Strength", &emissionStrength, 0.0f, 10.0f, "%.2f" ) )
                {
                    material->SetEmission( material->GetEmissionColor(), emissionStrength );
                }

                ImGui::EndTabItem();
            }

            // -----------------------------------------------------------------
            // Ambient Occlusion Tab
            // -----------------------------------------------------------------
            if ( ImGui::BeginTabItem( "AO" ) )
            {
                ImGui::Dummy( ImVec2( 0, 4 ) );
                float ao = material->GetAOValue();
                if ( ImGui::SliderFloat( "Intensity", &ao, 0.0f, 1.0f, "%.2f" ) )
                {
                    material->SetAO( ao );
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
} // namespace Desert::Editor