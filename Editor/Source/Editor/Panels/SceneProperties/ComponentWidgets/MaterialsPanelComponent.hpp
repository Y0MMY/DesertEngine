#pragma once

#include "IComponentWidget.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"

namespace Desert::Editor
{
    class MaterialComponentWidget
    {
    public:
        MaterialComponentWidget( const Assets::AssetManager* assetManager );

        void Render( ECS::Entity& entity );

    private:
        void RenderMaterialProperties( ECS::StaticMeshComponent& materialComp );

        // Number of material slots the mesh expects (one per submesh; 1 for primitives).
        size_t GetSubmeshCount( const ECS::StaticMeshComponent& meshComp ) const;
        // Creates a fresh PBR material asset on disk, registers its runtime material, returns its handle.
        Assets::AssetHandle CreateAndRegisterMaterial();
        // Resolves an asset path to a material, registers it if needed, and assigns it to a slot.
        void AssignMaterialFromPath( ECS::StaticMeshComponent& meshComp, size_t slot,
                                     const std::string& assetPath );

    private:
        std::unique_ptr<Editor::UI::UIHelper> m_UIHelper;
        const Assets::AssetManager*           m_AssetManager;
    };

} // namespace Desert::Editor