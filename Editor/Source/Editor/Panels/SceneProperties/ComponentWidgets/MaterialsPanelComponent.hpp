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
        // overriddenByShader: non-empty when the entity's Shader Override component routes the
        // mesh off the PBR path — the slots are shown collapsed with a notice.
        void RenderMaterialProperties( ECS::Entity& entity, ECS::StaticMeshComponent& materialComp,
                                       const std::string& overriddenByShader );


        // Unity-style shader picker inside the material (PBR (Standard) + Surface-domain DSL shaders).
        // Returns true when the shader changed (the runtime material must be rebuilt).
        bool DrawShaderPicker( Assets::SurfaceMaterialAsset& asset );

        // Schema-driven parameter editor for a custom-shader material; edits are persisted in the
        // asset's ShaderParams/ShaderTextures. Returns true when anything changed. In material-
        // INSTANCE mode (parentData/isInstance) the schema comes from the parent's shader,
        // non-overridden rows show the parent's values and edits write child overrides.
        bool DrawCustomShaderMaterial( Assets::SurfaceMaterialAsset& asset,
                                       const Assets::MaterialData* parentData = nullptr,
                                       bool                        isInstance = false );
        // Creates a child material-instance asset (.demat with ParentMaterialId) next to the other
        // materials and registers its shell (lazy — instances have no runtime Material of their own).
        Assets::AssetHandle CreateAndRegisterMaterialInstance( const Assets::SurfaceMaterialAsset& parent );

        // Number of material slots the mesh expects (one per submesh; 1 for primitives).
        size_t GetSubmeshCount( const ECS::StaticMeshComponent& meshComp ) const;
        // Creates a fresh PBR material asset on disk, registers its runtime material, returns its handle.
        Assets::AssetHandle CreateAndRegisterMaterial();
        // Resolves an asset path to a material, registers it if needed, and assigns it to a slot.
        void AssignMaterialFromPath( ECS::StaticMeshComponent& meshComp, size_t slot,
                                     const std::string& assetPath );
        // Grows MaterialSlots up to `slot` by repeating the effective (last) handle so an inherited
        // element row gains its own slot without changing the rendered look.
        static void MakeSlotExplicit( ECS::StaticMeshComponent& meshComp, size_t slot );

    private:
        std::unique_ptr<Editor::UI::UIHelper> m_UIHelper;
        const Assets::AssetManager*           m_AssetManager;
    };

} // namespace Desert::Editor