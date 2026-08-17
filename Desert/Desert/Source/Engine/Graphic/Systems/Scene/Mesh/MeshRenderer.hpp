#pragma once

#include <Common/Core/Units.hpp>

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Materials/MaterialOverrides.hpp>
#include <Engine/Graphic/Clouds/CloudWorldShadow.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialSilhouette.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialShadow.hpp>
#include <Engine/Graphic/Materials/Debug/MaterialDebugLine.hpp>
#include <Engine/Graphic/Materials/Debug/MaterialOverdraw.hpp>
#include <Engine/Graphic/Materials/Debug/MaterialOverdrawResolve.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/SkinnedMaterialPBR.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>
#include <Engine/Graphic/RenderGraphBuilder.hpp>

#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Geometry/StaticMesh.hpp>

#include <string>
#include <utility>
#include <unordered_map>

namespace Desert::Graphic::System
{
    struct MeshRenderData
    {
        class Mesh* Mesh;
        glm::mat4   Transform;

        // Pointer to the component's stable RuntimeSlotPtrs (valid for the frame) — passed by pointer through
        // the whole submission chain so no per-mesh slot-vector copy happens (Debug-heavy: this was ~0.9ms
        // of CmdBuffer ExecuteAll for 256 meshes).
        const std::vector<MaterialInstance*>* MaterialSlots = nullptr;

        // optional
        std::vector<glm::mat4> BoneMatrices;

        bool     Outlined        = false;
        uint64_t HiddenSubmeshes = 0;  // bit i = submesh i hidden (static meshes)
        int      ForcedLOD       = -1; // -1 = auto (by distance); 0..N pins a LOD level
        int      LODBias         = 0;  // shifts the auto LOD (ignored when forced)
        bool     CastShadows     = true;
        bool     ReceiveShadows  = true;
    };

    class MeshRenderer final : public RenderSystem
    {
    public:
        struct StaticMeshRenderData
        {
            class Desert::StaticMesh* Mesh      = nullptr;
            glm::mat4                 Transform = glm::mat4( 1.0f );
            // Pointer into the component's stable RuntimeSlotPtrs (valid for the frame) — no per-mesh copy.
            const std::vector<MaterialInstance*>* MaterialSlots   = nullptr;
            bool                                  Outlined        = false;
            uint64_t                              HiddenSubmeshes = 0;  // bit i = submesh i hidden
            int                                   ForcedLOD       = -1; // -1 = auto (by distance)
            int                                   LODBias         = 0;  // shifts the auto LOD (ignored when forced)
            bool                                  CastShadows     = true;
            bool                                  ReceiveShadows  = true;
        };

        struct SkinnedMeshRenderData
        {
            class Desert::SkinnedMesh*         Mesh     = nullptr;
            glm::mat4                          Transform = glm::mat4( 1.0f );
            class Graphic::SkinnedMaterialPBR* Material  = nullptr; // parent material (for Bind/executor)
            MaterialInstance*                  Instance  = nullptr; // instance applied during Bind
            std::vector<glm::mat4>             BoneMatrices;        // animated pose, or bind pose (identity)
            bool                               Outlined = false;
        };

        // A UE-style Instanced Static Mesh: ONE mesh + ONE PBR material drawn N times. The transforms come
        // straight from the component's array (pointer, stable for the frame) — no per-entity overhead.
        // Rendered through the SAME instanced pipeline/SSBO as the auto-batched static meshes.
        struct InstancedMeshRenderData
        {
            class Desert::StaticMesh*             Mesh      = nullptr;
            MaterialInstance*                     Material  = nullptr; // slot 0 (PBR)
            const std::vector<glm::mat4>*         Transforms = nullptr; // -> component's InstanceTransforms
        };

        // A static mesh drawn with a generic data-driven material. Two producers:
        //  - MaterialComponent (Shader Override) on the whole entity: ShaderName + Overrides drive a
        //    shader-keyed shared material (SlotMaterial == nullptr, all submeshes).
        //  - v3 per-slot materials: SlotMaterial points at the slot's own DataDrivenMaterial (asset
        //    params already applied) and VisibleSubmeshMask limits the draw to that slot's submeshes.
        struct GenericMeshRenderData
        {
            class Mesh*               Mesh      = nullptr;
            glm::mat4                 Transform = glm::mat4( 1.0f );
            std::string               ShaderName;
            Graphic::MaterialOverrides Overrides;
            bool                      Outlined = false; // selected -> JFA outline

            Graphic::Material*        SlotMaterial      = nullptr; // owned by MaterialService (stable)
            uint64_t                  VisibleSubmeshMask = ~0ull;  // bit i = submesh i drawn

            // A RUNTIME-owned texture bound straight to a sampler (bypasses the asset-handle
            // texture-override path). For procedural textures with no TextureAsset — e.g. the text
            // system's SDF font atlas. Non-owning: the producer keeps it alive for the frame.
            Graphic::Image2D* DirectTexture        = nullptr;
            std::string       DirectTextureSampler;
        };

        using RenderSystem::RenderSystem;

        // Everything the SCENE (not the object) contributes to a lit draw: the camera, the lights, the
        // shadow cascades and the IBL. Gathered ONCE per frame and applied to whichever material instance
        // is about to be bound.
        //
        // It is gathered in one place because it is one THING — and because it is the state that must
        // eventually move out of the shared material and into a per-renderer descriptor set (see
        // Docs/RENDERER_FRAME_STATE.md). Until then this is the single point every write goes through,
        // rather than the same four calls copied at three call sites.
        struct FrameState
        {
            const Core::Camera* Camera = nullptr;

            const ShaderProtocols::PointLight*     PointLights     = nullptr;
            const ShaderProtocols::SpotLight*      SpotLights      = nullptr;
            const ShaderProtocols::DirectionLight* DirectionLights = nullptr;

            const glm::mat4* CascadeViewProj = nullptr; // kNumCascades entries
            Image2D*         CascadeMaps[4]  = {};
            glm::vec4        CascadeTexelWorld{ 0.0f };
            float            ShadowBias      = 0.0f;
            bool             ShadowsEnabled  = true;
            int              ShadowDebugMode = 0;
            bool             ShowNormals     = false;
            bool             LightingDebug   = false;

            ImageCube* IrradianceMap  = nullptr;
            ImageCube* PrefilteredMap = nullptr;
            Image2D*   BrdfLut        = nullptr;

            // CLOUD SHADOWS ON THE WORLD: the sun-space map this frame traced, and the centre/sun/extent it
            // was traced with. Copied by value because that is what it is — four floats, four floats and a
            // borrowed image pointer the cloud renderer owns for the life of the view.
            CloudWorldShadowInput CloudWorldShadow;

            // Writes the whole snapshot onto @p instance's material. One call, so a new piece of frame
            // state can never be applied at two of the three sites and forgotten at the third.
            void ApplyTo( MaterialInstance* instance ) const;
        };

        // Gathers the snapshot from the scene renderer + this renderer's own cascade state. One place that
        // knows what "per-frame scene state" IS.
        FrameState CaptureFrameState( const Core::Camera* camera ) const;

        // Cascaded shadow maps: number of directional-shadow cascades (frustum splits) + per-map resolution.
        static constexpr uint32_t kNumCascades   = 4;
        static constexpr uint32_t kShadowMapSize = 2048;
        // How far from the camera shadows are computed at all — in WORLD UNITS, and a world unit is a
        // centimetre. It was a bare 150.0f from the metre era, which capped every shadow at a metre and a
        // half after the units switch.
        static inline const float kShadowMaxDistance = Common::Units::Metres( 150.0f );

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;
        virtual void                  RegisterPasses( RenderGraphBuilder& builder ) override;

        // Silhouette mask of the currently outlined meshes (white on the framebuffer clear color).
        // Consumed by JumpFloodOutlineRenderer to build the outline.
        // Deferred: renders the static-mesh queue into the scene renderer's G-buffer via a MANUAL render pass
        // (outside the graph — see the note in RegisterPasses). No-op unless the deferred pipeline exists.
        // Called by SceneRenderer when RenderPath == Deferred, before the deferred lighting pass.
        void RenderGBufferManual();
        // Forward transparent (glass) pass: draws meshes with material Transmission > 0 over the composited
        // scene. sceneColor = a snapshot of the opaque scene the glass samples for refraction (may be null).
        void RenderGlassManual( const std::shared_ptr<Image2D>& sceneColor );
        // Deferred path: draws the generic (custom-shader) meshes FORWARD over the deferred
        // lighting composite in a LOAD render pass — they have no G-buffer variant, so without
        // this they simply vanish in Deferred. Forward path draws them inside MeshGeometryPass.
        void RenderGenericManual();
        // Deferred path: draws SKINNED meshes forward over the deferred lighting composite (they have no
        // G-buffer variant, so without this they only appear in the silhouette/outline pass — invisible
        // otherwise). Forward path draws them inside MeshGeometryPass.
        void RenderSkinnedManual();
        // Reflective Shadow Map: the G-buffer rasterized from the SUN instead of the camera, into the scene
        // renderer's RSM buffer. Every lit texel becomes a virtual point light for the RSM GI mode, which is
        // what lets off-screen geometry bounce light. No-op unless the deferred pipeline exists.
        void RenderRSMManual();
        // World -> RSM clip for the pass above — the GI resolve needs it to project fragments into the
        // sun's view. Valid after UpdateCascades(); identity before the first frame.
        glm::mat4 GetRSMViewProj() const
        {
            return m_RSMViewProj;
        }

        const std::shared_ptr<Framebuffer>& GetSilhouetteMaskFramebuffer() const
        {
            return m_SilhouetteMaskFramebuffer;
        }

        // Overdraw debug view: re-rasterize every opaque mesh with additive blend (no depth) into a float
        // accumulation buffer, then heat-map the per-pixel overdraw count over the finished scene colour.
        // Path-independent (re-draws geometry; ignores the G-buffer), so it works in Forward and Deferred.
        void RenderOverdrawManual();

        const std::shared_ptr<Framebuffer>& GetOverdrawFramebuffer() const
        {
            return m_OverdrawFB;
        }

        // True if any queued mesh is flagged for the selection outline this frame. The Jump Flood pass
        // uses this to skip its (log2(width)) full-screen ping-pong passes when nothing is selected.
        bool HasOutline() const
        {
            for ( const auto& d : m_StaticQueue )
                if ( d.Outlined )
                    return true;
            for ( const auto& d : m_GenericQueue )
                if ( d.Outlined )
                    return true;
            for ( const auto& d : m_SkinnedQueue )
                if ( d.Outlined )
                    return true;
            return false;
        }

        void SubmitMesh( const MeshRenderData& data );
        void SubmitGenericMesh( const GenericMeshRenderData& data );
        void SubmitInstancedMesh( const InstancedMeshRenderData& data );
        void ClearQueues();

        // Debug wireframe toggle (SceneSettings.WireframeMode) — selects the line-polygon pipeline.
        void SetWireframe( bool enabled )
        {
            m_Wireframe = enabled;
        }

        // Distance-based mesh LOD (auto). LOD0 is byte-identical to the base geometry, so this only
        // affects meshes far from the camera. Toggle from the editor's Graphics menu.
        void SetLODEnabled( bool enabled )
        {
            m_LODEnabled = enabled;
        }
        bool IsLODEnabled() const
        {
            return m_LODEnabled;
        }

        // Which LOD level to draw for a mesh: a forced level (>= 0) wins; otherwise auto by screen
        // coverage (world bounding radius / camera distance), so big objects keep detail farther than
        // small ones. Returns 0 when LOD is off or there's no camera. Used by every mesh draw path.
        uint32_t ComputeLOD( const glm::mat4& transform, const class Desert::Mesh* mesh, int forcedLOD,
                             int lodBias = 0 ) const;

        // Cascaded shadow maps (R32F light-space depth, one framebuffer per cascade). Recompute the
        // per-cascade light matrices once per frame BEFORE the render graph records (intra-phase order is
        // nondeterministic). Called from SceneRenderer::OnUpdate.
        void UpdateCascades();

        // Cascade depth map (for the editor's CSM debug viewer). Null if out of range / not yet created.
        std::shared_ptr<Image2D> GetCascadeShadowImage( uint32_t cascade ) const
        {
            if ( cascade >= kNumCascades || !m_CascadeFB[cascade] )
                return nullptr;
            return m_CascadeFB[cascade]->GetColorAttachmentImage();
        }
        static constexpr uint32_t GetCascadeCount() { return kNumCascades; }

        void SetShadows( bool enabled, float bias, int debugMode, float splitLambda )
        {
            m_ShadowsEnabled  = enabled;
            m_ShadowBias      = bias;
            m_ShadowDebugMode = debugMode;
            m_SplitLambda     = splitLambda;
        }
        bool  AreShadowsEnabled() const { return m_ShadowsEnabled; }
        float GetShadowBias() const     { return m_ShadowBias; }
        // CSM data the deferred lighting pass needs to shadow the sun (same source the forward material uses).
        const glm::mat4* GetCascadeViewProj() const        { return m_CascadeVP; }
        const glm::vec4& GetCascadeWorldPerTexel() const   { return m_CascadeWorldPerTexel; }

        // Debug visualizations (Scene Settings -> Debug): per-pixel normals (PBR shader) + AABB wireframes.
        void SetDebugView( bool showNormals, bool showBoundingBoxes, const glm::vec3& bbColor,
                           float bbLineWidth, bool lightingDebug = false )
        {
            m_ShowNormals          = showNormals;
            m_ShowBoundingBoxes    = showBoundingBoxes;
            m_BoundingBoxColor     = bbColor;
            m_BoundingBoxLineWidth = bbLineWidth;
            m_LightingDebug        = lightingDebug;
        }

    private:
        bool SetupGeometryPass();
        bool SetupGBufferPass(); // deferred: static-mesh G-buffer write pipeline
        bool SetupGlassPass();   // forward transparent: static-mesh glass pipeline (blend, composites over scene)
        bool SetupSkinnedGeometryPass();
        bool SetupSilhouettePass();
        bool SetupShadowPass();

        void DrawStaticMeshes();
        void DrawSkinnedMeshes( bool useLoadPass = false );
        void DrawGenericMeshes( bool useLoadPass = false ); // per-object data-driven materials (v3 slots + overrides)
        void RegisterSilhouettePass( RenderGraphBuilder& builder );
        void RegisterShadowPass( RenderGraphBuilder& builder );
        bool SetupDebugLinePass();
        bool SetupOverdrawPass(); // overdraw accumulation pipeline + FB + fullscreen heat resolve
        void RegisterDebugPass( RenderGraphBuilder& builder );

        void UpdateGlobalUniforms( const Core::Camera* camera, const ShaderProtocols::PointLight& pointLights,
                                   const ShaderProtocols::DirectionLight& dirLights );

    private:
        // Static
        std::shared_ptr<GraphicsPipeline> m_StaticPipeline;
        std::shared_ptr<GraphicsPipeline> m_StaticWireframePipeline; // same spec, PolygonMode::Wireframe
        std::shared_ptr<GraphicsPipeline> m_StaticInstancedPipeline; // reads per-instance transform from SSBO
        bool                              m_Wireframe  = false;
        bool                              m_LODEnabled = true;

        // Deferred G-buffer geometry pipeline (static): writes Albedo+Metallic / Normal+Roughness into the
        // scene renderer's MRT G-buffer instead of shading. Same vertex layout + material bindings as the
        // forward static pipeline, so the same StaticMaterialPBR data binds. Null if the shader is missing.
        std::shared_ptr<Shader>           m_StaticGBufferShader;
        std::shared_ptr<GraphicsPipeline> m_StaticGBufferPipeline;
        bool                              m_DeferredGeometry = false; // set true only while drawing the G-buffer pass
        std::shared_ptr<Shader>           m_StaticGlassShader;
        std::shared_ptr<GraphicsPipeline> m_StaticGlassPipeline;
        bool                              m_GlassPass = false; // set true only while drawing the transparent glass pass
        // DEDICATED glass material (never drawn by the opaque passes) so its per-frame UB ring is written ONCE
        // per frame in the glass pass — sharing an opaque material across two passes/frame hangs the GPU.
        std::unique_ptr<StaticMaterialPBR> m_GlassMaterial;
        MaterialInstancePtr                m_GlassInstance;

        // Reflective Shadow Map (G-buffer from the sun) — the off-screen bounce source for the RSM GI mode.
        // Its camera UB carries the SUN's matrices, so like glass it needs its OWN material: sharing one with
        // the opaque passes would write the same per-frame UB twice in a frame. m_RSMViewProj/m_RSMEye come
        // from cascade 1 in UpdateCascades(), so the pass draws through a STANDARD-Z matrix and needs its
        // own pipeline (m_RSMPipeline) rather than the reversed-Z G-buffer one — see SetupDeferredPass.
        std::unique_ptr<StaticMaterialPBR> m_RSMMaterial;
        MaterialInstancePtr                m_RSMInstance;
        std::shared_ptr<GraphicsPipeline>  m_RSMPipeline;
        glm::mat4                          m_RSMViewProj = glm::mat4( 1.0f );
        glm::vec3                          m_RSMEye      = glm::vec3( 0.0f );

        std::shared_ptr<Shader>   m_GeometryShader;
        std::shared_ptr<Shader>   m_InstancedGeometryShader;

        // Auto-batching: identical (same parent material + same Mesh*) static meshes are collapsed into one
        // hardware-instanced draw via this shared material. Per-instance model matrices are packed into its
        // InstanceTransforms SSBO; the shared scene data (camera/lights/shadow/env) is uploaded once/frame.
        std::unique_ptr<Graphic::StaticMaterialPBRInstanced> m_StaticInstancedMaterial;
        MaterialInstancePtr                                  m_StaticInstancedInstance;

        // Skinned
        std::shared_ptr<GraphicsPipeline> m_SkinnedPipeline;
        std::shared_ptr<Shader>   m_SkinnedShader;

        // Silhouette (mask for the Jump Flood outline)
        std::shared_ptr<GraphicsPipeline>   m_SilhouettePipeline;
        std::shared_ptr<Shader>             m_SilhouetteShader;
        std::unique_ptr<MaterialSilhouette> m_SilhouetteMaterial;
        std::shared_ptr<Framebuffer>        m_SilhouetteMaskFramebuffer;
        // Skinned silhouette (selected skinned meshes -> outline). Skins by the Bones SSBO so the mask
        // matches the posed/animated mesh. Optional — null if the Silhouette_Skinned shader is missing.
        std::shared_ptr<GraphicsPipeline>          m_SilhouetteSkinnedPipeline;
        std::shared_ptr<Shader>                    m_SilhouetteSkinnedShader;
        std::unique_ptr<MaterialSilhouetteSkinned> m_SilhouetteSkinnedMaterial;

        // Cascaded directional shadow maps: one framebuffer + one MaterialShadow (its own light-matrix UB,
        // so the 4 cascade passes don't alias a shared UBO) per cascade. m_CascadeVP is recomputed each
        // frame by UpdateCascades().
        std::shared_ptr<GraphicsPipeline> m_ShadowPipeline;
        std::shared_ptr<Shader>           m_ShadowShader;
        std::unique_ptr<MaterialShadow>   m_ShadowMaterial[kNumCascades];
        std::shared_ptr<Framebuffer>      m_CascadeFB[kNumCascades];

        // Instanced shadow caster: one pipeline + per-cascade instanced material (each owns the cascade's
        // light matrix UBO + an InstanceTransforms SSBO). Batched casters of one mesh collapse to a single
        // instanced draw per cascade. Optional — null if the Shadow_Instanced shader is missing.
        std::shared_ptr<GraphicsPipeline>        m_ShadowInstancedPipeline;
        std::shared_ptr<Shader>                  m_ShadowInstancedShader;
        std::unique_ptr<MaterialShadowInstanced> m_ShadowInstancedMaterial[kNumCascades];
        glm::mat4                         m_CascadeVP[kNumCascades] = { glm::mat4( 1.0f ) };
        // World-space size of one shadow-map texel per cascade (2*radius/res) — drives a cascade-correct
        // normal-offset/bias in the PBR shader instead of the old fixed world-unit constants.
        glm::vec4                         m_CascadeWorldPerTexel    = glm::vec4( 1.0f );
        bool                              m_ShadowsEnabled  = true;
        float                             m_ShadowBias      = 0.005f;
        int                               m_ShadowDebugMode = 0;     // ShadowDebugMode (Off/ShadowFactor/Cascades)
        float                             m_SplitLambda     = 0.6f;  // cascade split uniform<->log blend

        // Debug visualization (Scene Settings -> Debug)
        bool      m_ShowNormals          = false; // per-pixel normal color (PBR shader branch)
        bool      m_LightingDebug        = false; // per-light colored "where light lands" (PBR shader branch)
        bool      m_ShowBoundingBoxes    = false; // AABB wireframes via the debug line renderer below
        glm::vec3 m_BoundingBoxColor     = glm::vec3( 0.25f, 0.95f, 0.35f );
        float     m_BoundingBoxLineWidth = 1.5f;

        // Debug line renderer (AABB wireframes): Lines-topology pipeline + storage-buffer line verts.
        std::shared_ptr<GraphicsPipeline>  m_DebugLinePipeline;
        std::shared_ptr<Shader>            m_DebugLineShader;
        std::unique_ptr<MaterialDebugLine> m_DebugLineMaterial;

        // Overdraw view: geometry accumulation (additive, no depth) into m_OverdrawFB, then a fullscreen
        // resolve that heat-maps the count over the scene colour. Static/generic meshes only (skinned skipped).
        std::shared_ptr<GraphicsPipeline>        m_OverdrawPipeline;
        std::shared_ptr<Shader>                  m_OverdrawShader;
        std::unique_ptr<MaterialOverdraw>        m_OverdrawMaterial;
        std::shared_ptr<Framebuffer>             m_OverdrawFB;
        std::shared_ptr<GraphicsPipeline>        m_OverdrawResolvePipeline;
        std::shared_ptr<Shader>                  m_OverdrawResolveShader;
        std::unique_ptr<MaterialOverdrawResolve> m_OverdrawResolveMaterial;

        std::vector<StaticMeshRenderData>  m_StaticQueue;
        std::vector<SkinnedMeshRenderData> m_SkinnedQueue;

        // Generic (data-driven) static meshes + a per-shader DataDrivenMaterial cache (one material reused
        // across all meshes of that shader; per-object data is the transform push-constant + overrides).
        std::vector<GenericMeshRenderData>                              m_GenericQueue;
        std::unordered_map<std::string, std::unique_ptr<DataDrivenMaterial>> m_GenericMaterials;

        // UE-style Instanced Static Meshes (one entity = N instances). Folded into the shared instanced
        // pipeline/SSBO alongside the auto-batched static meshes (geometry + shadow passes).
        std::vector<InstancedMeshRenderData> m_InstancedQueue;

    private:
        // fallbacks
        std::unique_ptr<Graphic::StaticMaterialPBR>  m_StaticMaterialFallback;
        std::unique_ptr<Graphic::SkinnedMaterialPBR> m_SkinnedMaterialFallback;

        // ── Per-frame scratch (memory discipline: reuse, don't reallocate) ──────────────────
        // Cleared each use; capacity persists across frames so the steady state allocates nothing.

        // One draw-ready record per object: the effective material is built ONCE per object per
        // frame and reused for the glass split, the batch entry and the per-object SSBO.
        struct ObjDraw
        {
            const StaticMeshRenderData* Obj  = nullptr;
            MaterialInstance*           Inst = nullptr;
            PBRGpuMaterial              Gm{};
            bool                        HasOverrides = false;
        };
        struct InstancedDraw
        {
            Desert::StaticMesh* Mesh          = nullptr;
            uint32_t            InstanceCount = 0;
            uint32_t            FirstInstance = 0;
            uint32_t            MaterialIndex = 0;
        };
        struct ShadowBatch
        {
            Desert::StaticMesh* Mesh  = nullptr;
            uint32_t            Count = 0;
            uint32_t            First = 0;
        };

        std::vector<glm::mat4>      m_ScratchInstTransforms; // geometry + shadow instanced SSBOs
        std::vector<PBRGpuMaterial> m_ScratchInstMaterials;
        std::vector<InstancedDraw>  m_ScratchInstDraws;
        std::vector<PBRGpuMaterial> m_ScratchGpuMaterials; // per-object Materials[] SSBO
        std::vector<ObjDraw>        m_ScratchSingles;
        std::vector<ShadowBatch>    m_ScratchShadowBatches;
        std::vector<const StaticMeshRenderData*> m_ScratchShadowSingles;
    };
} // namespace Desert::Graphic::System
