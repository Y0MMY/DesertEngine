#include "StaticMeshComponent.hpp"
#include <ImGui/imgui.h>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Engine/Geometry/Mesh.hpp>

#include "MaterialsPanelComponent.hpp"

#include "Helper/MeshDetailsWidget.hpp"

#include "../../MeshEditor/MeshEditorPanel.hpp"

#include <Engine/Geometry/PrimitiveMeshFactory.hpp>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    StaticMeshComponentWidget::StaticMeshComponentWidget( const Assets::AssetManager* assetManager )
         : ComponentWidget( "3D Model" ), m_AssetManager( assetManager )
    {
    }

    void StaticMeshComponentWidget::Render( ECS::Entity& entity, ::Desert::Core::Scene* scene )
    {
        auto& staticMesh = entity.GetComponent<ECS::StaticMeshComponent>();

        Utils::ImGuiUtilities::PushID();
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 2, 2 ) );

        ImGui::Columns( 2 );
        ImGui::Separator();

        ImGui::TextUnformatted( "Mesh Type" );
        ImGui::NextColumn();
        ImGui::PushItemWidth( -1 );

        const char* meshTypes[] = { "Asset", "Primitive" };
        int currentType = staticMesh.Primitive.has_value() ? 1 : 0;
        if ( ImGui::Combo( "##MeshType", &currentType, meshTypes, IM_ARRAYSIZE( meshTypes ) ) )
        {
            if ( currentType == 0 )
                staticMesh.Primitive.reset();
            else
                staticMesh.Primitive = Geometry::PrimitiveType::Cube;
        }

        ImGui::PopItemWidth();
        ImGui::NextColumn();

        if ( !staticMesh.Primitive.has_value() )
        {
            ImGui::TextUnformatted( "Asset" );
            ImGui::NextColumn();
            ImGui::PushItemWidth( -1 );

            std::string currentSelectionName = "Select Mesh";
            if ( staticMesh.MeshHandle )
            {
                auto meshAsset = m_AssetManager->FindByHandle<Assets::MeshAsset>( staticMesh.MeshHandle );
                if ( meshAsset )
                {
                    currentSelectionName = Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                }
            }

            if ( ImGui::Button( currentSelectionName.c_str(), ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            {
                ImGui::OpenPopup( "mesh_selector" );
            }

            if ( ImGui::BeginPopup( "mesh_selector" ) )
            {
                auto meshAssets = m_AssetManager->FindAllByType<Assets::MeshAsset>();
                static ImGuiTextFilter meshFilter;
                meshFilter.Draw( "##Search", 200 );
                ImGui::Separator();

                for ( const auto& [handle, meshAsset] : meshAssets )
                {
                    const auto isSkinnedOpt = Runtime::ResourceRegistry::GetMeshService()->IsSkinned( handle );
                    if ( isSkinnedOpt.has_value() && isSkinnedOpt.value() )
                    {
                        continue;
                    }

                    const std::string& meshName = Common::Utils::FileSystem::GetFileName( meshAsset->GetMetadata().Filepath );
                    if ( meshFilter.PassFilter( meshName.c_str() ) )
                    {
                        if ( ImGui::Selectable( meshName.c_str(), staticMesh.MeshHandle == handle ) )
                        {
                            SetMeshAsset( staticMesh, handle );
                        }
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopItemWidth();
            ImGui::NextColumn();
        }
        else
        {
            ImGui::TextUnformatted( "Shape" );
            ImGui::NextColumn();
            ImGui::PushItemWidth( -1 );

            const char* shapes[] = { "Cube", "Sphere", "Pyramid", "Plane", "Cylinder", "Capsule" };
            int currentShape = (int)staticMesh.Primitive.value();
            if ( ImGui::Combo( "##Shape", &currentShape, shapes, IM_ARRAYSIZE( shapes ) ) )
            {
                staticMesh.Primitive = (Geometry::PrimitiveType)currentShape;
                // MeshECSSystem will handle the dynamic mesh generation/update
            }

            ImGui::PopItemWidth();
            ImGui::NextColumn();

            ImGui::TextUnformatted( "Geometry Editor" );
            ImGui::NextColumn();
            if ( ImGui::Button( "Open Mesh Editor", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
            {
                if ( auto* meshEditor = MeshEditorPanel::GetInstance() )
                {
                    meshEditor->SetTarget( entity );
                }
            }
            ImGui::NextColumn();
        }

        ImGui::Columns( 1 );
        ImGui::Separator();

        {
            static MaterialComponentWidget materialComponent( m_AssetManager );
            materialComponent.Render( entity );
        }

        ImGui::PopStyleVar();
        Utils::ImGuiUtilities::PopID();
    }

    void StaticMeshComponentWidget::SetMeshAsset( ECS::StaticMeshComponent& staticMesh, const Assets::AssetHandle& handle )
    {
        staticMesh.MeshHandle = handle;
        staticMesh.Primitive.reset();
        // Load default materials from asset...
    }

    std::string StaticMeshComponentWidget::GetPrimitiveName( const ECS::StaticMeshComponent& staticMesh ) const
    {
        if ( !staticMesh.Primitive ) return "None";
        switch ( *staticMesh.Primitive )
        {
            case Geometry::PrimitiveType::Cube: return "Cube";
            case Geometry::PrimitiveType::Sphere: return "Sphere";
            case Geometry::PrimitiveType::Plane: return "Plane";
            default: return "Primitive";
        }
    }

    DESERT_REGISTER_CUSTOM_COMPONENT(
         ECS::StaticMeshComponent, "3D Model", false,
         ( []( ECS::Entity& e, ::Desert::Core::Scene* s, const ComponentEditContext& ctx )
           { StaticMeshComponentWidget( ctx.AssetMgr() ).Render( e, s ); } ) )

} // namespace Desert::Editor
