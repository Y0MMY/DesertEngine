#pragma once

#include <Engine/Graphic/Systems/RenderSystem.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialSilhouette.hpp>
#include <Engine/Graphic/Materials/Mesh/MaterialShadow.hpp>
#include <Engine/Graphic/Materials/Debug/MaterialDebugLine.hpp>
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

        std::vector<MaterialInstance*> MaterialSlots;

        // optional
        std::vector<glm::mat4> BoneMatrices;

        bool Outlined = false;
    };

    class MeshRenderer final : public RenderSystem
    {
    public:
        struct StaticMeshRenderData
        {
            class Desert::StaticMesh*      Mesh = nullptr;
            glm::mat4                      Transform = glm::mat4( 1.0f );
            std::vector<MaterialInstance*> MaterialSlots;
            bool                           Outlined = false;
        };

        struct SkinnedMeshRenderData
        {
            class Desert::SkinnedMesh*         Mesh;
            glm::mat4                          Transform;
            class Graphic::SkinnedMaterialPBR* Material;
            std::vector<glm::mat4>             BoneMatrices;
            bool                               Outlined = false;
        };

        // A static mesh drawn with a generic data-driven material (assigned via MaterialComponent with a
        // non-PBR shader). Rendered per-object (no SSBO batching) — additive to the PBR path.
        struct GenericMeshRenderData
        {
            class Mesh*                                    Mesh = nullptr;
            glm::mat4                                      Transform = glm::mat4( 1.0f );
            std::string                                    ShaderName;
            std::vector<std::pair<std::string, glm::vec4>> ParamOverrides;
            std::vector<std::pair<std::string, uint64_t>>  TextureOverrides; // name -> texture asset handle
            bool                                           Outlined = false; // selected -> JFA outline
        };

        using RenderSystem::RenderSystem;

        // Cascaded shadow maps: number of directional-shadow cascades (frustum splits) + per-map resolution.
        static constexpr uint32_t kNumCascades   = 4;
        static constexpr uint32_t kShadowMapSize = 2048;

        virtual Common::BoolResultStr Initialize() override;
        virtual void                  Shutdown() override;
        virtual void                  RegisterPasses( RenderGraphBuilder& builder ) override;

        // Silhouette mask of the currently outlined meshes (white on the framebuffer clear color).
        // Consumed by JumpFloodOutlineRenderer to build the outline.
        const std::shared_ptr<Framebuffer>& GetSilhouetteMaskFramebuffer() const
        {
            return m_SilhouetteMaskFramebuffer;
        }

        void SubmitMesh( const MeshRenderData& data );
        void SubmitGenericMesh( const GenericMeshRenderData& data );
        void ClearQueues();

        // Debug wireframe toggle (SceneSettings.WireframeMode) — selects the line-polygon pipeline.
        void SetWireframe( bool enabled )
        {
            m_Wireframe = enabled;
        }

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
        bool SetupSkinnedGeometryPass();
        bool SetupSilhouettePass();
        bool SetupShadowPass();

        void DrawStaticMeshes();
        void DrawSkinnedMeshes();
        void DrawGenericMeshes(); // per-object data-driven materials (MaterialComponent on a mesh)
        void RegisterSilhouettePass( RenderGraphBuilder& builder );
        void RegisterShadowPass( RenderGraphBuilder& builder );
        bool SetupDebugLinePass();
        void RegisterDebugPass( RenderGraphBuilder& builder );

        void UpdateGlobalUniforms( const Core::Camera* camera, const ShaderProtocols::PointLight& pointLights,
                                   const ShaderProtocols::DirectionLight& dirLights );

    private:
        // Static
        std::shared_ptr<GraphicsPipeline> m_StaticPipeline;
        std::shared_ptr<GraphicsPipeline> m_StaticWireframePipeline; // same spec, PolygonMode::Wireframe
        bool                              m_Wireframe = false;
        std::shared_ptr<Shader>   m_GeometryShader;

        // Skinned
        std::shared_ptr<GraphicsPipeline> m_SkinnedPipeline;
        std::shared_ptr<Shader>   m_SkinnedShader;

        // Silhouette (mask for the Jump Flood outline)
        std::shared_ptr<GraphicsPipeline>   m_SilhouettePipeline;
        std::shared_ptr<Shader>             m_SilhouetteShader;
        std::unique_ptr<MaterialSilhouette> m_SilhouetteMaterial;
        std::shared_ptr<Framebuffer>        m_SilhouetteMaskFramebuffer;

        // Cascaded directional shadow maps: one framebuffer + one MaterialShadow (its own light-matrix UB,
        // so the 4 cascade passes don't alias a shared UBO) per cascade. m_CascadeVP is recomputed each
        // frame by UpdateCascades().
        std::shared_ptr<GraphicsPipeline> m_ShadowPipeline;
        std::shared_ptr<Shader>           m_ShadowShader;
        std::unique_ptr<MaterialShadow>   m_ShadowMaterial[kNumCascades];
        std::shared_ptr<Framebuffer>      m_CascadeFB[kNumCascades];
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

        std::vector<StaticMeshRenderData>  m_StaticQueue;
        std::vector<SkinnedMeshRenderData> m_SkinnedQueue;

        // Generic (data-driven) static meshes + a per-shader DataDrivenMaterial cache (one material reused
        // across all meshes of that shader; per-object data is the transform push-constant + overrides).
        std::vector<GenericMeshRenderData>                              m_GenericQueue;
        std::unordered_map<std::string, std::unique_ptr<DataDrivenMaterial>> m_GenericMaterials;

    private:
        // fallbacks
        std::unique_ptr<Graphic::StaticMaterialPBR>  m_StaticMaterialFallback;
        std::unique_ptr<Graphic::SkinnedMaterialPBR> m_SkinnedMaterialFallback;
    };
} // namespace Desert::Graphic::System
