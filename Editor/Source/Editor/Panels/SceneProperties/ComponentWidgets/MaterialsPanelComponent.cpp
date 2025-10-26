#include "MaterialsPanelComponent.hpp"
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <ImGui/imgui.h>
#include <Common/Utilities/FileSystem.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <glm/gtc/type_ptr.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    MaterialComponentWidget::MaterialComponentWidget( const std::weak_ptr<Assets::AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    void MaterialComponentWidget::Render( ECS::Entity& entity )
    {
        auto& materialComp = entity.GetComponent<ECS::StaticMeshComponent>();
        RenderMaterialProperties( materialComp );
    }

    static void RenderField( const std::string& fieldName, auto&  property )
    {
        auto& value = property.get();

        Utils::ImGuiUtilities::PropertyFlag flags = Utils::ImGuiUtilities::PropertyFlag::None;

        if ( property.template has_attribute<ColorAttribute>() )
        {
            flags = ( Utils::ImGuiUtilities::PropertyFlag )(
                 (int)flags | (int)Utils::ImGuiUtilities::PropertyFlag::ColorProperty );
        }

        if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, float> )
        {

            float minVal = -FLT_MAX;
            float maxVal = FLT_MAX;
            float step   = 0.01f;

            if ( auto range_attr = property.template get_attribute<RangeAttribute>() )
            {
                minVal = range_attr->min_value;
                maxVal = range_attr->max_value;
                step   = range_attr->step;
            }

            flags = ( Utils::ImGuiUtilities::PropertyFlag )( (int)flags |
                                                             (int)Utils::ImGuiUtilities::PropertyFlag::DragValue );
            Utils::ImGuiUtilities::Property( fieldName.c_str(), value, minVal, maxVal, 0.01f, flags );

        }
        else if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, glm::vec3> )
        {
            Utils::ImGuiUtilities::Property( fieldName.c_str(), value, -FLT_MAX, FLT_MAX, false, flags );
        }
        else if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, glm::vec4> )
        {
            Utils::ImGuiUtilities::Property( fieldName.c_str(), *(glm::vec3*)&value, -FLT_MAX, FLT_MAX, true,
                                             flags );
        }
        else if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, int> )
        {
            Utils::ImGuiUtilities::Property( fieldName.c_str(), *(uint32_t*)&value, flags );
        }
        else if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, bool> )
        {
            Utils::ImGuiUtilities::Property( fieldName.c_str(), value, flags );
        }
        else
        {
            ImGui::Columns( 2 );
            ImGui::SetColumnWidth( 0, 150.0f );

            ImGui::TextUnformatted( fieldName.c_str() );
            ImGui::NextColumn();
            ImGui::TextDisabled( "Unsupported type" );
            ImGui::NextColumn();
            ImGui::Columns( 1 );
        }

    }

    void MaterialComponentWidget::RenderMaterialProperties( ECS::StaticMeshComponent& materialComp )
    {
        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        if ( ImGui::TreeNodeEx( "Materials", ImGuiTreeNodeFlags_Framed ) )
        {
            auto material = materialComp.Material;
            int  matIndex = 0;

            {
                if ( !material )
                    return;

                std::string matName;
                if ( matName.empty() )
                {
                    matName = "Material " + std::to_string( matIndex );
                }

                ImGui::Indent();

                if ( ImGui::TreeNodeEx( (void*)(intptr_t)material.get(), ImGuiTreeNodeFlags_Framed,
                                        matName.c_str() ) )
                {
                    ImGui::Indent();
                    ImGui::PushID( (int)(uintptr_t)material.get() );

                    // Material name editing
                    static bool        materialNameUpdated = false;
                    static std::string renamedMaterialName;

                    if ( Utils::ImGuiUtilities::InputText( matName, "##materialName" ) )
                    {
                        materialNameUpdated = true;
                        renamedMaterialName = matName;
                    }

                    // Save to file button
                    if ( ImGui::Button( "Save to file", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                    {
                        // Implementation for saving material to file
                    }

                    material->VisitUniformFields(
                         [this]( const auto& uniformName, const auto& field_name, auto& property )
                         { 
                            RenderField( property.GetDisplayName(), (property)); } );

                    ImGui::Unindent();
                    ImGui::TreePop();
                    ImGui::PopID();
                }
                ImGui::Unindent();

                matIndex++;
            }

            // Add new material button
            if ( ImGui::Button( "Add Material", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
            {
                /* auto newMaterial = std::make_shared<Graphics::Material>();
                 materialComp.AddMaterial( newMaterial );*/
            }

            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

} // namespace Desert::Editor