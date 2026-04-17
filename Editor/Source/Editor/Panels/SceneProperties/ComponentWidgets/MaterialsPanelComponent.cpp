#include "MaterialsPanelComponent.hpp"
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Texture.hpp>
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
        m_UIHelper = std::make_unique<Editor::UI::UIHelper>();
        m_UIHelper->Init();
    }

    void MaterialComponentWidget::Render( ECS::Entity& entity )
    {
        auto& materialComp = entity.GetComponent<ECS::StaticMeshComponent>();
        RenderMaterialProperties( materialComp );
    }

    static void RenderTextureField( const std::unique_ptr<Editor::UI::UIHelper>& helperUI,
                                    const std::string& fieldName, auto& property, bool flipImage = false )
    {
        auto& value = property.get();

        ImGui::Columns( 2 );
        ImGui::SetColumnWidth( 0, 150.0f );

        ImGui::TextUnformatted( fieldName.c_str() );
        ImGui::NextColumn();

        auto displayImage2D = [&]( const Desert::Graphic::Image2DRef& image, const char* hoverText = "Texture" )
        {
            if ( image )
            {
                helperUI->Image( image, ImVec2( 128, 128 ) );

                if ( ImGui::IsItemHovered() )
                {
                    ImGui::BeginTooltip();
                    ImGui::Text( "%s", hoverText );
                    ImGui::Text( "Size: %dx%d", image->GetWidth(), image->GetHeight() );
                    ImGui::EndTooltip();
                }
                return true;
            }
            return false;
        };

        auto displayPlaceholder = [&]( const char* text ) { ImGui::Button( text, ImVec2( 128, 128 ) ); };

        auto handleDragDrop = [&]( const char* payloadType, auto&& dropHandler )
        {
            if ( ImGui::BeginDragDropTarget() )
            {
                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( payloadType ) )
                {
                    dropHandler( payload );
                }
                ImGui::EndDragDropTarget();
            }
        };

        //// Handle Texture2D
        // if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::Texture2DRef> )
        //{
        //     if ( value )
        //     {
        //         auto image = value->GetImage2D();
        //         if ( !displayImage2D( image, "Texture2D" ) )
        //         {
        //             displayPlaceholder( "Invalid Texture" );
        //         }
        //     }
        //     else
        //     {
        //         displayPlaceholder( "Drop Texture Here" );
        //     }

        //    handleDragDrop( "TEXTURE_ASSET",
        //                    [&]( const ImGuiPayload* payload )
        //                    {
        //                        // Handle texture drop
        //                    } );
        //}
        //// Handle TextureCube
        // else if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::TextureCubeRef> )
        //{
        //     if ( value )
        //     {
        //         auto image = value->GetImageCube();
        //         if ( image )
        //         {
        //             displayPlaceholder( "Cubemap" );

        //            if ( ImGui::IsItemHovered() )
        //            {
        //                ImGui::BeginTooltip();
        //                ImGui::Text( "Cubemap Texture" );
        //                ImGui::Text( "Size: %dx%d", image->GetWidth(), image->GetHeight() );
        //                ImGui::EndTooltip();
        //            }
        //        }
        //    }
        //    else
        //    {
        //        displayPlaceholder( "Drop Cubemap Here" );
        //    }

        //    handleDragDrop( "CUBEMAP_ASSET",
        //                    [&]( const ImGuiPayload* payload )
        //                    {
        //                        // Handle cubemap drop
        //                    } );
        //}
        // Handle Image2D
        if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::Image2DRef> )
        {
            if ( !displayImage2D( value, "Image2D" ) )
            {
                displayPlaceholder( "No Image" );
            }
        }
        // Handle ImageCube
        else if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::ImageCubeRef> )
        {
            if ( value )
            {
                displayPlaceholder( "Cubemap" );

                if ( ImGui::IsItemHovered() )
                {
                    ImGui::BeginTooltip();
                    ImGui::Text( "Cubemap Image" );
                    ImGui::Text( "Size: %dx%d", value->GetWidth(), value->GetHeight() );
                    ImGui::EndTooltip();
                }
            }
            else
            {
                displayPlaceholder( "No Cubemap" );
            }
        }

        ImGui::NextColumn();
        ImGui::Columns( 1 );
    }

    static void RenderField( const std::unique_ptr<Editor::UI::UIHelper>& helperUI, const std::string& fieldName,
                             auto& property )
    {
        /*auto& value = property.get();

        Utils::ImGuiUtilities::PropertyFlag flags = Utils::ImGuiUtilities::PropertyFlag::None;

        if ( property.template has_attribute<ColorAttribute>() )
        {
            flags = ( Utils::ImGuiUtilities::PropertyFlag )(
                 (int)flags | (int)Utils::ImGuiUtilities::PropertyFlag::ColorProperty );
        }

        if constexpr ( std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::Texture2DRef> ||
                       std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::TextureCubeRef> ||
                       std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::Image2DRef> ||
                       std::is_same_v<std::decay_t<decltype( value )>, Desert::Graphic::ImageCubeRef> )
        {
            RenderTextureField( helperUI, fieldName, property );
            return;
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
        }*/
    }

    void MaterialComponentWidget::RenderMaterialProperties( ECS::StaticMeshComponent& materialComp )
    {
        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        if ( ImGui::TreeNodeEx( "Materials", ImGuiTreeNodeFlags_Framed ) )
        {
            auto material = materialComp.MaterialSlots;
            int  matIndex = 0;

            {
                if ( !material.empty() )
                {
                    ImGui::TextUnformatted( "Empty Material" );
                    if ( ImGui::Button( "Add Material", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                    {
                        // Implementation for adding new material
                        // materialComp.Material = MaterialFactory::CreateDefaultMaterial();
                    }
                    ImGui::TreePop();
                    ImGui::PopStyleVar();
                    Utils::ImGuiUtilities::PopID();
                    return;
                }

                std::string matName;
                if ( matName.empty() )
                {
                    matName = "Material " + std::to_string( matIndex );
                }

                ImGui::Indent();

                static bool        materialNameUpdated = false;
                static std::string renamedMaterialName;

                if ( materialNameUpdated )
                {
                    if ( matName == renamedMaterialName )
                    {
                        materialNameUpdated = false;
                        ImGui::SetNextItemOpen( true );
                    }
                }

                if ( ImGui::TreeNodeEx( (void*)(intptr_t)material[0], ImGuiTreeNodeFlags_Framed, matName.c_str()))
                {
                    ImGui::Indent();
                    ImGui::PushID( (int)(uintptr_t)material[0]);

                    // Material name editing
                    if ( Utils::ImGuiUtilities::InputText( matName, "##materialName" ) )
                    {
                        materialNameUpdated = true;
                        renamedMaterialName = matName;
                        // material->SetName( matName ); // Uncomment if material has SetName method
                    }

                    // Save to file button
                    if ( ImGui::Button( "Save to file", ImVec2( ImGui::GetContentRegionAvail().x, 0.0f ) ) )
                    {
                        // Implementation for saving material to file
                    }

                    // Render flags section
                    ImGui::Columns( 2 );
                    ImGui::Separator();

                    // Example render flags - you'll need to adapt these to your material system
                    bool twoSided     = false; // material->GetFlag( Desert::Graphic::Material::TWOSIDED );
                    bool depthTested  = true;  // material->GetFlag( Desert::Graphic::Material::DEPTHTEST );
                    bool alphaBlended = false; // material->GetFlag( Desert::Graphic::Material::ALPHABLEND );
                    bool castShadows  = true;  // !material->GetFlag( Desert::Graphic::Material::NOSHADOW );

                    ImGui::AlignTextToFramePadding();

                    if ( Utils::ImGuiUtilities::Property( "Alpha Blended", alphaBlended ) )
                    {
                        // material->SetFlag( Desert::Graphic::Material::ALPHABLEND, alphaBlended );
                    }

                    if ( Utils::ImGuiUtilities::Property( "Two Sided", twoSided ) )
                    {
                        // material->SetFlag( Desert::Graphic::Material::TWOSIDED, twoSided );
                    }

                    if ( Utils::ImGuiUtilities::Property( "Depth Tested", depthTested ) )
                    {
                        // material->SetFlag( Desert::Graphic::Material::DEPTHTEST, depthTested );
                    }

                    if ( Utils::ImGuiUtilities::Property( "Cast Shadows", castShadows ) )
                    {
                        // material->SetFlag( Desert::Graphic::Material::NOSHADOW, !castShadows );
                    }

                    ImGui::Columns( 1 );

                    //// Render material properties dynamically
                    // material->VisitUniformFields(
                    //      [this]( const auto& uniformName, const auto& field_name, auto& property )
                    //      {
                    //          // Group texture properties together
                    //          if constexpr ( std::is_same_v<std::decay_t<decltype( property.get() )>,
                    //                                        Desert::Graphic::Texture2DRef> ||
                    //                         std::is_same_v<std::decay_t<decltype( property.get() )>,
                    //                                        Desert::Graphic::TextureCubeRef> ||
                    //                         std::is_same_v<std::decay_t<decltype( property.get() )>,
                    //                                        Desert::Graphic::Image2DRef> ||
                    //                         std::is_same_v<std::decay_t<decltype( property.get() )>,
                    //                                        Desert::Graphic::ImageCubeRef> )
                    //          {
                    //              if ( ImGui::TreeNodeEx( property.GetDisplayName().c_str(),
                    //                                      ImGuiTreeNodeFlags_Framed ) )
                    //              {
                    //                  RenderField( m_UIHelper, property.GetDisplayName(), property );
                    //                  ImGui::TreePop();
                    //              }
                    //          }
                    //          else
                    //          {
                    //              RenderField( m_UIHelper, property.GetDisplayName(), property );
                    //          }
                    //      } );

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
                // Implementation for adding new material
                // auto newMaterial = MaterialFactory::CreateDefaultMaterial();
                // materialComp.AddMaterial( newMaterial );
            }

            ImGui::TreePop();
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

} // namespace Desert::Editor