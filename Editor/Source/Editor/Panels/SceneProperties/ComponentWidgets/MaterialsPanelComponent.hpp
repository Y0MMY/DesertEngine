#pragma once

#include "IComponentWidget.hpp"

#include <Editor/Core/Selection/SelectionManager.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include "Editor/Widgets/UIHelper/ImGuiUI.hpp"
#include <Editor/Widgets/ThumbnailCache.hpp>

#include <array>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor
{
    class MaterialComponentWidget
    {
    public:
        MaterialComponentWidget( const Assets::AssetManager* assetManager );

        // @p scene supplies the active camera for the "drawing LOD n" readout; without it that line is
        // simply omitted.
        void Render( ECS::Entity& entity, ::Desert::Core::Scene* scene = nullptr );

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
        // baseName is sanitized into the filename ("M_<Entity>"); identity stays the in-file GUID.
        Assets::AssetHandle CreateAndRegisterMaterial( const std::string& baseName = "Material" );
        // Resolves an asset path to a material, registers it if needed, and assigns it to a slot.
        void AssignMaterialFromPath( ECS::StaticMeshComponent& meshComp, size_t slot,
                                     const std::string& assetPath );
        // Grows MaterialSlots up to `slot` by repeating the effective (last) handle so an inherited
        // element row gains its own slot without changing the rendered look.
        static void MakeSlotExplicit( ECS::StaticMeshComponent& meshComp, size_t slot );

        // A slot's visual identity, read from the SHADER SCHEMA rather than from hardcoded PBR names:
        // the first Color parameter as a swatch, plus up to two of its ranged scalars as bars. For the
        // standard shader that is albedo / metallic / roughness; a custom shader gets the same treatment
        // from its own Properties block, for free.
        struct SwatchStrip
        {
            struct Bar
            {
                const char* Label = nullptr; // points into the shader schema (owned by the shader service)
                float       Value = 0.0f;    // normalised into 0..1 by the param's Range (bar fill)
                float       Raw   = 0.0f;    // the value as authored (what the number reads)
            };

            bool               HasColor = false;
            glm::vec4          Color    = glm::vec4( 1.0f );
            std::array<Bar, 2> Bars;
            uint32_t           BarCount = 0;
        };

        // parentData: material-instance mode — values fall back to the parent chain, then to the schema
        // default, exactly like the parameter editor.
        static SwatchStrip BuildSwatchStrip( const Assets::SurfaceMaterialAsset& asset,
                                             const Assets::MaterialData*         parentData );
        // Draws the strip right-aligned INTO the element header bar (pure ImDrawList — it must not
        // become an item that steals the header's click/drop handling).
        static void DrawSwatchStripInHeader( const SwatchStrip& strip );
        // The identity card inside an open element: rendered thumbnail (or an albedo chip when the asset
        // browser has not produced one yet), material name, shader, and the labelled swatch bars.
        // parentData/parentName describe the instance's parent (both empty for a base material).
        void DrawSlotIdentityCard( const Assets::SurfaceMaterialAsset& asset, const SwatchStrip& strip,
                                   const Assets::MaterialData* parentData, const std::string& parentName );

    private:
        std::unique_ptr<Editor::UI::UIHelper> m_UIHelper;
        const Assets::AssetManager*           m_AssetManager;
        // Decoded rendered-thumbnail PNGs (the shared on-disk cache the asset browser fills).
        ThumbnailCache m_Thumbnails;
    };

} // namespace Desert::Editor