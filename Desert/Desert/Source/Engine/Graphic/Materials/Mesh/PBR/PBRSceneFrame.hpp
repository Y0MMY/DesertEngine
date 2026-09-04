#pragma once

#include <Engine/Graphic/Materials/Mesh/PBR/MaterialPBRBase.hpp>

namespace Desert::Graphic
{
    /**
     * Everything the SCENE (not the object) contributes to a lit PBR draw: the camera, the lights, the
     * shadow cascades, the IBL environment and the cloud layer's shadow. Gathered ONCE per frame
     * (MeshRenderer::CaptureFrameState) and applied to whichever material instance is about to be bound.
     *
     * It lives beside the materials rather than inside MeshRenderer because it is the PAYLOAD of the PBR
     * materials — a material's Bind() can then require the WHOLE snapshot instead of being handed a
     * selection of it. That is the defect this type was moved here to close: the skinned material used
     * to take the camera, the lights and the cloud shadow as three separate fields of its own Bind()
     * argument, so the two pieces of scene state that were NOT in that list — the shadow cascades and
     * the environment cubes — reached every static mesh in the engine and no skinned one.
     *
     * Neither slot was garbage, which is why nothing ever crashed or warned:
     * VulkanMaterialBackend::InitializeWithFallbacks seeds every declared binding before anything real
     * reaches it. `ShadowUB` therefore held the zero-filled dummy buffer — `u_ShadowParams.y == 0`, so
     * ShadowFactor() returned 1.0 and the cascades were silently OFF — and the environment trio held
     * its fallback images, which sample BLACK (the fallback cube is created with white pixels, but
     * VulkanImageCube::UploadData is an empty function, so nothing is ever put in it), leaving the
     * split-sum ambient at zero and a skinned surface lit by the sun and the anti-black floor alone.
     * Measured on a probe scene: a skinned box read a flat 0.453 luminance at saturation 0.008 against
     * an identical static cube's 0.703 lit / 0.566 shadowed at saturation 0.139 / 0.287.
     *
     * There is deliberately no second constructor and no per-field setter: the one producer fills it and
     * the one applier writes it. It is also the state that must eventually move out of the shared
     * material and into a per-renderer descriptor set (the contract's per-frame renderer state rule);
     * until then this is the single point every write goes through, rather than the same five calls
     * copied at each call site.
     */
    struct PBRSceneFrame
    {
        const Core::Camera* Camera = nullptr;

        const ShaderProtocols::PointLight*     PointLights     = nullptr;
        const ShaderProtocols::SpotLight*      SpotLights      = nullptr;
        const ShaderProtocols::DirectionLight* DirectionLights = nullptr;

        const glm::mat4* CascadeViewProj = nullptr; // MaterialPBRBase::kMaxCascades entries
        Image2D*         CascadeMaps[MaterialPBRBase::kMaxCascades] = {};
        glm::vec4        CascadeTexelWorld{ 0.0f };
        float            ShadowBias      = 0.0f;
        bool             ShadowsEnabled  = true;
        int              ShadowDebugMode = 0;
        bool             ShowNormals     = false;
        bool             LightingDebug   = false;

        ImageCube* IrradianceMap  = nullptr;
        ImageCube* PrefilteredMap = nullptr;
        Image2D*   BrdfLut        = nullptr;

        // The cloud layer's shadow on the sun — the SECOND occluder, beside the cascades above. It
        // belongs in this snapshot for the reason the snapshot exists: it is scene state, one per
        // frame, and it has to reach the opaque pass, the glass pass, the RSM and the skinned pass
        // identically. While it did not, the only surfaces in the engine that received a cloud shadow
        // were the ones a deferred composite happened to shade.
        CloudShadowInput CloudShadow;

        // Writes the whole snapshot onto @p instance's material. One call, so a new piece of frame
        // state can never be applied at four of the five sites and forgotten at the fifth.
        //
        // It reaches materials of several different shaders (StaticMeshPBR, StaticMeshPBR_Instanced,
        // StaticMeshGlass, SkinnedMeshPBR), which need not declare these resources at the same SLOT
        // NUMBERS — every write goes through Material::Get by NAME, which is what makes one applier
        // able to serve all of them.
        void ApplyTo( MaterialInstance* instance ) const;
    };
} // namespace Desert::Graphic
