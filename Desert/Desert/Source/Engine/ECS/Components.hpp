#pragma once

#include <entt/entt.hpp>

#include <Common/Core/UUID.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Geometry/PrimitiveType.hpp>
#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Assets/Common.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/SkinnedMaterialPBR.hpp>

#include <Engine/Animation/Animator.hpp>
#include <Engine/Animation/FSM/AnimationStateMachine.hpp>

#include <Engine/Physics/PhysicsWorld.hpp>
#include <Engine/Scripting/ScriptProperty.hpp>

#include <Engine/Reflection/ReflectionMacros.hpp>

namespace Desert
{
    class Mesh;
    class DynamicMesh;
    class SkinnedMesh;
    namespace Animation
    {
        class Skeleton;
        namespace Graph
        {
            struct AnimGraph;
            class Evaluator;
        } // namespace Graph
    } // namespace Animation
} // namespace Desert

namespace Desert::Graphic
{
    class Image2D;
}

namespace Desert::ECS
{
    struct TagComponent
    {
        std::string Tag;
    };

    struct UUIDComponent
    {
        Common::UUID UUID;
    };

    // "Reflected render-data block": editable, reflected fields the editor draws and the renderer maps
    // to its GPU representation. This is the general concept — a surface Material (PBRSurfaceParams) is
    // just ONE specialization; camera and lights are others. NOT a material, hence the member is `Data`.
    struct CameraData
    {
        REFLECT()

        PROPERTY( DisplayName( "Main Camera" ), Category( "Camera" ) )
        bool IsMainCamera = true;

        PROPERTY( DisplayName( "Field of View" ), Category( "Camera" ), Range( 10.0f, 120.0f ),
                  Header( "Projection" ), Tooltip( "Vertical field of view, in degrees." ) )
        float FOV = 45.0f;

        PROPERTY( DisplayName( "Near" ), Category( "Camera" ), Range( 0.01f, 10.0f ),
                  Tooltip( "Near clip plane distance. Anything closer is not drawn." ) )
        float Near = 0.1f;

        PROPERTY( DisplayName( "Far" ), Category( "Camera" ), Range( 10.0f, 10000.0f ),
                  Tooltip( "Far clip plane distance. Anything beyond is not drawn." ) )
        float Far = 1000.0f;
    };

    struct CameraComponent
    {
        std::shared_ptr<Core::Camera> Camera;
        CameraData                    Data;
    };

    struct VisibilityComponent
    {
        bool Visible = true;
    };

    struct StaticMeshComponent
    {
        Assets::AssetHandle              MeshHandle;
        std::vector<Assets::AssetHandle> MaterialSlots;
        std::vector<Graphic::MaterialInstancePtr>
             RuntimeMaterialInstances; // Cache to keep instances alive and avoid per-frame allocations
        std::vector<Graphic::MaterialInstance*>
             RuntimeSlotPtrs; // Raw-pointer view of RuntimeMaterialInstances, rebuilt only when instances change
                              // so the per-frame render path passes a pointer instead of allocating+copying a slot
                              // vector every frame (Debug-heavy)
        std::optional<Geometry::PrimitiveType> Primitive;   // Optional primitive type for dynamic generation
        std::shared_ptr<DynamicMesh>           RuntimeMesh; // Unique mesh instance for modifications
        bool                                   OutlineDraw = false;
        int                                    ForcedLOD   = -1; // -1 = auto (by distance); 0..N pins a LOD
        int  LODBias        = 0;    // shifts the AUTO-picked LOD (+coarser, -finer); ignored when ForcedLOD >= 0
        bool CastShadows    = true; // false = skipped by the shadow (depth) passes
        bool ReceiveShadows = true; // false = sun shadows are not applied to this mesh (forward path)
        // Per-submesh visibility: bit i set = submesh i is HIDDEN (skipped at draw). 0 = all visible. Up to
        // 64 submeshes; edited per Element in the Materials panel.
        uint64_t HiddenSubmeshes = 0;
        // Transient: MaterialService invalidation stamp the runtime instances were built against;
        // a mismatch forces a rebuild (see MeshECSSystem) so parent Material* can never dangle.
        uint32_t SeenMaterialsVersion = 0;
    };

    // World-space SDF text. FontService bakes the .ttf into an SDF atlas; TextECSSystem lays the
    // string out into a per-entity quad mesh and draws it through the generic path with the TextSDF
    // shader — into the HDR composite, so EmissiveIntensity > ~1 blooms like any emissive surface.
    struct TextComponent
    {
        std::string         Text = "Text";
        Assets::AssetHandle Font; // SDF font asset (drag a .ttf or pick a preloaded one);
                                  // unset = the engine's built-in default (Roboto).
        glm::vec4 Color             = glm::vec4( 1.0f );
        float     Size              = 1.0f;  // world units per em (scales the baked metrics)
        float     EmissiveIntensity = 1.0f;  // >1 => the text blooms
        bool      Billboard         = false; // face the camera (added by the system per frame)

        // Transient: the laid-out glyph-quad mesh, rebuilt only when the text/font/size changes.
        std::shared_ptr<DynamicMesh> RuntimeMesh;
        std::string                  BuiltText;
        std::string                  BuiltFont; // resolved ttf path used for the current mesh (font-change guard)
        float                        BuiltSize = 0.0f;
    };

    struct SkinnedMeshComponent
    {
        Assets::AssetHandle              MeshHandle;
        std::vector<Assets::AssetHandle> MaterialSlots;
        std::vector<Graphic::MaterialInstancePtr>
                 RuntimeMaterialInstances; // Cache to keep instances alive and avoid per-frame allocations
        uint32_t SeenMaterialsVersion = 0; // see StaticMeshComponent

        // In-editor rig: a skinned mesh built at runtime by "Convert to Skinned" (from a static mesh + placed
        // bones, auto-weighted), NOT yet a cooked asset. When set, RuntimeMesh overrides MeshHandle in the
        // render path. RuntimeSkeleton owns the Skeleton that RuntimeMesh references by raw pointer, keeping it
        // alive for the mesh's lifetime (destroyed together with the component).
        std::shared_ptr<SkinnedMesh>         RuntimeMesh;
        std::shared_ptr<Animation::Skeleton> RuntimeSkeleton;
    };

    // UE-style Instanced Static Mesh: ONE mesh + ONE material drawn N times (per-instance world transforms)
    // as a single instanced draw call. The per-instance transforms live in a GPU storage buffer the
    // instanced shader reads by gl_InstanceIndex, so 1000 instances cost ~1 draw + 1 material setup instead
    // of N. Use for repeated static props / NPCs / buildings in the city.
    struct InstancedStaticMeshComponent
    {
        Assets::AssetHandle              MeshHandle;
        std::vector<Assets::AssetHandle> MaterialSlots;
        std::vector<glm::mat4>           InstanceTransforms; // per-instance world matrices

        // Transient runtime state (not serialized): generated mesh for primitives, the material instance,
        // and a dirty flag so the renderer re-uploads the instance SSBO only when the transforms change.
        std::optional<Geometry::PrimitiveType>    Primitive;
        std::shared_ptr<DynamicMesh>              RuntimeMesh;
        std::vector<Graphic::MaterialInstancePtr> RuntimeMaterialInstances;
        bool                                      InstancesDirty       = true;
        uint32_t                                  SeenMaterialsVersion = 0; // see StaticMeshComponent
    };

    // A FOLIAGE type (UE5-style). Sits alongside an InstancedStaticMeshComponent (the mesh + per-instance
    // WORLD transforms, drawn instanced). The Foliage paint tool scatters instances of this type onto surfaces
    // (raycast brush). These are the per-type scatter params.
    struct FoliageComponent
    {
        float Density       = 6.0f; // instances scattered per paint dab (in the brush disk)
        float ScaleMin      = 0.8f;
        float ScaleMax      = 1.3f;
        float ZOffsetMin    = 0.0f; // sink(-)/raise(+) along world up, randomized per instance
        float ZOffsetMax    = 0.0f;
        float MaxPitchDeg   = 0.0f; // random tilt off the up/normal axis (0 = upright)
        float SlopeMinDeg   = 0.0f; // only paint where the surface slope is within [min,max] degrees
        float SlopeMaxDeg   = 90.0f;
        bool  AlignToNormal = true; // tilt instances to the surface normal
        bool  RandomYaw     = true; // random rotation about the up axis
    };

    // Per-layer splat mode. Auto = weight from height/slope rules (in-shader); Manual = weight painted
    // into the terrain splat map (brush, Stage 3b); Off = layer disabled. Reflected -> combo in editor.
    enum class TerrainLayerMode
    {
        Auto,
        Manual,
        Off
    };

    // Procedural heightmap terrain params (reflected -> inspector + serialization).
    struct TerrainData
    {
        REFLECT()

        PROPERTY( DisplayName( "Size" ), Category( "Terrain" ), Range( 1.0f, 500.0f ) )
        float Size = 50.0f;

        PROPERTY( DisplayName( "Resolution" ), Category( "Terrain" ), Range( 2.0f, 256.0f ) )
        int Resolution = 64;

        PROPERTY( DisplayName( "Height Scale" ), Category( "Terrain" ), Range( 0.0f, 50.0f ) )
        float HeightScale = 5.0f;

        PROPERTY( DisplayName( "Noise Frequency" ), Category( "Terrain" ), Range( 0.001f, 1.0f ) )
        float NoiseFrequency = 0.08f;

        PROPERTY( DisplayName( "Seed" ), Category( "Terrain" ), Range( 0.0f, 9999.0f ) )
        int Seed = 1337;

        PROPERTY( DisplayName( "Grass Layer" ), Category( "Terrain Layers" ) )
        TerrainLayerMode GrassMode = TerrainLayerMode::Auto;

        PROPERTY( DisplayName( "Rock Layer" ), Category( "Terrain Layers" ) )
        TerrainLayerMode RockMode = TerrainLayerMode::Auto;

        PROPERTY( DisplayName( "Snow Layer" ), Category( "Terrain Layers" ) )
        TerrainLayerMode SnowMode = TerrainLayerMode::Auto;

        // --- GPU-instanced grass (Stage 7). Density-gated by the splat grass channel. ---
        PROPERTY( DisplayName( "Enable Grass" ), Category( "Grass" ) )
        bool EnableGrass = false;

        // Blades per side over the terrain -> GrassDensity^2 instances. Higher = blades closer together
        // (smaller spacing = size/GrassDensity). 512 => up to ~262k blades.
        PROPERTY( DisplayName( "Grass Density" ), Category( "Grass" ), Range( 8.0f, 512.0f ) )
        int GrassDensity = 320;

        PROPERTY( DisplayName( "Grass Height" ), Category( "Grass" ), Range( 0.05f, 5.0f ) )
        float GrassHeight = 0.4f;

        // Geometric blades grown per clump instance. Higher = denser tufts (fills gaps, esp. for short
        // grass) at more vertices/instance. Drives the indirect draw's vertex count (blades*24).
        PROPERTY( DisplayName( "Blades Per Clump" ), Category( "Grass" ), Range( 1.0f, 12.0f ) )
        int GrassBladesPerClump = 5;

        // Blade width multiplier (1 = ~fills the grid cell). Raise to widen blades and close gaps; lower
        // for thinner, more individual blades.
        PROPERTY( DisplayName( "Grass Width" ), Category( "Grass" ), Range( 0.1f, 5.0f ) )
        float GrassWidth = 1.0f;

        // Brightness multiplier on the grass color — lower = darker grass, higher = lighter. 1.0 = the
        // natural shaded green. Replaces the old RGB tint (a single light/dark slider is what's wanted).
        PROPERTY( DisplayName( "Grass Brightness" ), Category( "Grass" ), Range( 0.2f, 2.0f ) )
        float GrassBrightness = 1.0f;
    };

    // TerrainECSSystem generates a grid mesh from Data into the entity's StaticMeshComponent.RuntimeMesh (so
    // the normal mesh render path draws it). Regenerated when any param changes (tracked via BuiltHash).
    struct TerrainComponent
    {
        TerrainData Data;
        // Transient: hash of the params the current RuntimeMesh was built from; a mismatch -> regenerate.
        std::size_t BuiltHash = 0;

        // --- Splat painting (Stage 3b, runtime only; not yet serialized) ---
        // RGBA8 splat map: R=grass, G=rock, B=snow weights. Manual-mode layers sample this. The CPU mirror
        // is the brush's edit target; SplatDirty triggers a safe GPU re-upload (ViewportPanel::OnPreUpdate).
        static constexpr uint32_t         SplatResolution = 256;
        std::shared_ptr<Graphic::Image2D> SplatMap;
        std::vector<unsigned char>        SplatPixels; // size = SplatResolution^2 * 4, lazily allocated
        bool                              SplatDirty = false;
    };

    // One overridden material parameter (keyed by the shader's #pragma param name). vec4 stores any
    // scalar/vector value (float uses .x, color uses rgba). Data-driven: NOT a fixed C++ field per param.
    struct MaterialParamOverride
    {
        std::string Name;
        glm::vec4   Value = glm::vec4( 0.0f );
    };

    // One overridden texture param (keyed by the shader's #pragma param texture2D name) -> texture asset.
    struct MaterialTextureOverride
    {
        std::string Name;
        uint64_t    TextureHandle = 0; // Assets::AssetHandle as uint64 (0 = unset -> shader fallback)
    };

    // Assigns an arbitrary shader (by program name) to whatever renderer draws this entity, with its
    // parameters edited generically in Details (built from the shader's #pragma param schema). The
    // renderer builds a DataDrivenMaterial from ShaderName and applies these overrides.
    struct MaterialComponent
    {
        std::string                          ShaderName;
        std::vector<MaterialParamOverride>   Params;   // scalar/vector overrides (on top of #pragma defaults)
        std::vector<MaterialTextureOverride> Textures; // texture2D overrides (unset -> backend fallback)
    };

    struct AnimationComponent
    {
        // active Animator (runtime instance)
        std::unique_ptr<Animation::Animator> Animator;
        // std::unique_ptr<Animation::AnimationStateMachine> StateMachine;

        // current name (debug / editor / FSM)
        std::string CurrentClip;

        bool Playing = true;
        bool Loop    = true;

        float PlaybackSpeed = 1.0f;

        // Root motion
        bool EnableRootMotion = false;

        // Notify names fired by the Animator THIS frame (crossed clip markers). Filled by AnimationECSSystem,
        // drained + dispatched to the entity's scripts (OnAnimationNotify) by ScriptSystem. Transient.
        std::vector<std::string> PendingNotifies;

        // AnimGraph (Phase 4): a data-driven state machine that PICKS the clip to play from live parameters.
        // When Graph is set, AnimationECSSystem drives the Animator from the evaluator instead of CurrentClip.
        // In-memory only for now (not serialized with the scene). GraphRevision is bumped by the editor on any
        // structural edit so the ECS rebuilds the transient evaluator; parameters are set live on the evaluator.
        std::shared_ptr<Animation::Graph::AnimGraph> Graph;
        std::shared_ptr<Animation::Graph::Evaluator> GraphEvaluator; // transient runtime state
        uint32_t                                     GraphRevision      = 0;
        uint32_t                                     BuiltGraphRevision = 0; // ECS: rev the evaluator was built at

        AnimationComponent() = default;

        explicit AnimationComponent( std::unique_ptr<Animation::Animator>&& animator )
             : Animator( std::move( animator ) )
        {
        }
    };

    // Data-driven state -> clip mapping for LocomotionSystem, so the SYSTEM holds NO clip knowledge (no clip
    // names or instances baked in). The system maps planar speed / on-ground to one of these clip NAMES and
    // hands it to AnimationComponent.CurrentClip; the clips themselves come from the AnimationLibrary (imported
    // assets or the procedural humanoid's built-in clips). LocomotionSystem falls back to a default-constructed
    // instance of THIS struct when the component is absent, so the defaults live in data, not in the system.
    struct LocomotionComponent
    {
        std::string IdleClip  = "Idle";
        std::string WalkClip  = "Walk";
        std::string RunClip   = "Run";
        std::string JumpClip  = "Jump";
        float       WalkSpeed = 0.2f; // planar speed above which -> walk
        float       RunSpeed  = 6.5f; // planar speed above which -> run
    };

    // Blendshape / morph-target weights for the entity's mesh. Weights[k] (0..1, though over/undershoot is
    // allowed) scales morph target k of the mesh asset; the CPU blend is base + Σ(weight·delta) (see
    // Geometry::ApplyMorphTargets). TargetNames mirrors the mesh's target names for the Details UI and stays
    // index-aligned with Weights. Non-reflected (like AnimationComponent) — edited via the Morph widget.
    struct MorphComponent
    {
        std::vector<float>       Weights;
        std::vector<std::string> TargetNames;

        // Last weights the runtime blended into the entity's geometry — lets a per-frame apply skip work
        // when nothing changed. Transient (not serialized).
        std::vector<float> AppliedWeights;
    };

    struct TransformComponent
    {
        glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation    = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale       = { 1.0f, 1.0f, 1.0f };

        glm::mat4 GetTransform() const
        {
            return glm::translate( glm::mat4( 1.0f ), Translation ) * glm::toMat4( glm::quat( Rotation ) ) *
                   glm::scale( glm::mat4( 1.0f ), Scale );
        }
    };

    struct DirectionalLightData
    {
        REFLECT()

        PROPERTY( DisplayName( "Color" ), Category( "Light" ), Color )
        glm::vec3 Color = glm::vec3( 1.0f );

        PROPERTY( DisplayName( "Intensity" ), Category( "Light" ), Range( 0.0f, 10.0f ) )
        float Intensity = 1.0f;
    };

    struct DirectionLightComponent
    {
        DirectionalLightData Data;
    };

    // Distance falloff model for a point light. Reflected enum — drives a combo in the editor and
    // round-trips through reflected serialization. (Consumed by the lighting upload path later.)
    enum class LightFalloff
    {
        Linear,
        Quadratic,
        InverseSquare
    };

    struct PointLightData
    {
        REFLECT()

        PROPERTY( DisplayName( "Color" ), Category( "Light" ), Color )
        glm::vec3 Color = glm::vec3( 1.0f );

        PROPERTY( DisplayName( "Intensity" ), Category( "Light" ), Range( 0.0f, 10.0f ) )
        float Intensity = 1.0f;

        PROPERTY( DisplayName( "Radius" ), Category( "Light" ), Range( 0.0f, 100.0f ) )
        float Radius = 10.0f;

        // Inner radius where attenuation == 1 (a small emitter "source size"); falloff runs from here to
        // Radius. 0 = point source.
        PROPERTY( DisplayName( "Min Radius" ), Category( "Light" ), Range( 0.0f, 100.0f ) )
        float MinRadius = 0.0f;

        PROPERTY( DisplayName( "Falloff" ), Category( "Light" ) )
        LightFalloff Falloff = LightFalloff::Quadratic;

        PROPERTY( DisplayName( "Show Radius" ), Category( "Light" ) )
        bool ShowRadius = false;
    };

    struct PointLightComponent
    {
        PointLightData Data;
    };

    // Spot light: a cone of light from the entity's position (transform translation) aimed along the
    // entity's forward (transform rotation). Inner/Outer cone angles (degrees) give a soft edge.
    struct SpotLightData
    {
        REFLECT()

        PROPERTY( DisplayName( "Color" ), Category( "Light" ), Color )
        glm::vec3 Color = glm::vec3( 1.0f );

        PROPERTY( DisplayName( "Intensity" ), Category( "Light" ), Range( 0.0f, 10.0f ) )
        float Intensity = 1.0f;

        PROPERTY( DisplayName( "Range" ), Category( "Light" ), Range( 0.0f, 100.0f ) )
        float Range = 15.0f;

        PROPERTY( DisplayName( "Inner Cone" ), Category( "Light" ), Range( 0.0f, 89.0f ) )
        float InnerConeAngle = 20.0f; // degrees — full intensity inside this half-angle

        PROPERTY( DisplayName( "Outer Cone" ), Category( "Light" ), Range( 0.0f, 90.0f ) )
        float OuterConeAngle = 30.0f; // degrees — zero intensity outside this half-angle

        PROPERTY( DisplayName( "Falloff" ), Category( "Light" ) )
        LightFalloff Falloff = LightFalloff::Quadratic;

        PROPERTY( DisplayName( "Show Cone" ), Category( "Light" ) )
        bool ShowCone = false;
    };

    struct SpotLightComponent
    {
        SpotLightData Data;
    };

    // How particle billboards composite into the scene. Additive = glowing FX (fire/sparks/magic);
    // AlphaBlend = soft opaque puffs (smoke/dust). Reflected enum -> editor combo + serialization.
    enum class ParticleBlendMode
    {
        Additive,
        AlphaBlend
    };

    // GPU-simulated billboard particle emitter. The reflected fields below are the AUTHORING parameters
    // (Details UI + scene serialization are generated from them); the actual simulation runs in a compute
    // shader and the quads are drawn camera-facing in the Transparency phase (ParticleRenderer). Emits from
    // the entity's transform. Runtime GPU buffers live on the render system, keyed by the entity — not here.
    struct ParticleEmitterData
    {
        REFLECT()

        PROPERTY( DisplayName( "Enabled" ), Category( "Emitter" ) )
        bool Enabled = true;

        PROPERTY( DisplayName( "Max Particles" ), Category( "Emitter" ), Range( 1.0f, 100000.0f ) )
        int MaxParticles = 2000;

        PROPERTY( DisplayName( "Spawn Rate" ), Category( "Emitter" ), Range( 0.0f, 10000.0f ) )
        float SpawnRate = 200.0f; // particles per second

        PROPERTY( DisplayName( "Looping" ), Category( "Emitter" ) )
        bool Looping = true;

        PROPERTY( DisplayName( "Simulate In World" ), Category( "Emitter" ) )
        bool WorldSpace = true; // world = particles trail behind a moving emitter; local = ride with it

        PROPERTY( DisplayName( "Lifetime" ), Category( "Particle" ), Range( 0.01f, 60.0f ) )
        float Lifetime = 3.0f; // seconds

        PROPERTY( DisplayName( "Lifetime Variance" ), Category( "Particle" ), Range( 0.0f, 1.0f ) )
        float LifetimeVariance = 0.2f;

        PROPERTY( DisplayName( "Start Speed" ), Category( "Motion" ), Range( 0.0f, 100.0f ) )
        float StartSpeed = 2.0f;

        PROPERTY( DisplayName( "Speed Variance" ), Category( "Motion" ), Range( 0.0f, 1.0f ) )
        float SpeedVariance = 0.3f;

        PROPERTY( DisplayName( "Emit Direction" ), Category( "Motion" ) )
        glm::vec3 Direction = glm::vec3( 0.0f, 1.0f, 0.0f ); // normalized emit axis

        PROPERTY( DisplayName( "Cone Angle" ), Category( "Motion" ), Range( 0.0f, 180.0f ) )
        float ConeAngle = 45.0f; // degrees of spread around Direction (wide enough to read from any angle)

        PROPERTY( DisplayName( "Gravity" ), Category( "Motion" ) )
        glm::vec3 Gravity = glm::vec3( 0.0f, -2.0f, 0.0f );

        PROPERTY( DisplayName( "Start Size" ), Category( "Look" ), Range( 0.0f, 10.0f ) )
        float StartSize = 0.25f;

        // Size-over-life ease: the compute shader raises the normalized age t to this power before lerping
        // Start->End size. 1 = linear; <1 = fast then slow (puffs); >1 = slow then fast (shrinking sparks).
        // Authored as a curve in the Particle Editor.
        PROPERTY( DisplayName( "Size Curve Power" ), Category( "Look" ), Range( 0.1f, 8.0f ) )
        float SizeCurvePower = 1.0f;

        PROPERTY( DisplayName( "End Size" ), Category( "Look" ), Range( 0.0f, 10.0f ) )
        float EndSize = 0.06f; // keep a sliver of size so particles stay readable instead of vanishing mid-life

        PROPERTY( DisplayName( "Start Color" ), Category( "Look" ), Color )
        glm::vec3 StartColor = glm::vec3( 1.0f, 0.6f, 0.15f );

        PROPERTY( DisplayName( "End Color" ), Category( "Look" ), Color )
        glm::vec3 EndColor = glm::vec3( 0.6f, 0.1f, 0.0f );

        PROPERTY( DisplayName( "Start Alpha" ), Category( "Look" ), Range( 0.0f, 1.0f ) )
        float StartAlpha = 1.0f;

        PROPERTY( DisplayName( "End Alpha" ), Category( "Look" ), Range( 0.0f, 1.0f ) )
        float EndAlpha = 0.0f;

        // AlphaBlend (over) by default: it shows the particle colour against ANY background. Additive glow
        // washes out against bright/lit surfaces — looked down at a sunlit floor the fountain "disappeared"
        // even though it was drawn, while it popped against the dark sky from below. Fire/sparks presets in
        // the Particle Editor still switch this to Additive where the scene behind them is dark.
        PROPERTY( DisplayName( "Blend" ), Category( "Look" ) )
        ParticleBlendMode Blend = ParticleBlendMode::AlphaBlend;
    };

    struct ParticleEmitterComponent
    {
        ParticleEmitterData Data;
    };

    // ============================================================
    // UI — Godot-Control-style screen-space UI (2D)
    // ============================================================

    // Horizontal text alignment within a UI element's rect. Reflected enum -> editor combo + serialization.
    enum class UITextAlign
    {
        Left,
        Center,
        Right
    };

    // Vertical placement of the (possibly multi-line) text block within the element rect.
    enum class UITextVAlign
    {
        Top,
        Middle,
        Bottom
    };

    // What to do when the text is wider/taller than the element rect (mutually resolved in this order:
    // AutoSize shrinks first, then Wrap breaks lines, then Ellipsis truncates the overflow).
    enum class UITextOverflow
    {
        Overflow, // draw past the rect (legacy behaviour)
        Ellipsis, // truncate the overflowing tail with "…"
        Clip      // hard-clip to the rect (no ellipsis)
    };

    // How the canvas maps to the viewport (Unity CanvasScaler-style):
    //  Stretch        - canvas == the WHOLE viewport, 1:1 pixels. Layout is driven by anchors, so a full-screen
    //                   element (anchors 0,0-1,1) fills any resolution and nothing "zooms" when the window
    //                   resizes. The resolution-independent default.
    //  ScaleWithScreen- canvas == the whole viewport too, but the ENTIRE design is scaled from the reference
    //                   resolution (offsets + font sizes multiplied), so a layout authored at 1280x720 keeps
    //                   its proportions on any screen. MatchWidthHeight blends width- vs height-based scaling.
    //  Letterbox      - the reference resolution scaled to FIT inside the viewport and centred (black bars).
    //                   For fixed-aspect, pixel-perfect designs.
    enum class UICanvasScaleMode
    {
        Stretch,
        ScaleWithScreen,
        Letterbox
    };

    // Where the canvas lives. ScreenSpace = a flat overlay (menus/HUD). WorldSpace = billboarded at the
    // canvas entity's 3D position, projected to the screen + distance-scaled each frame (nameplate over an
    // NPC, a floating panel). WorldSpace needs the camera's view-proj (passed by the renderer's caller).
    enum class UICanvasRenderMode
    {
        ScreenSpace,
        WorldSpace
    };

    // Auto-layout container type. A UILayoutGroup on an element positions + sizes its DIRECT children
    // automatically (overriding their anchors) — the Unity/Godot "layout group" model.
    enum class UILayoutType
    {
        Vertical,   // VBox: children top -> bottom
        Horizontal, // HBox: children left -> right
        Grid        // fixed cells, wrapping into rows
    };

    // Add to an element to auto-arrange its children. Children keep their UILayout for appearance + preferred
    // size (from CustomMinimumSize, else the offset size), but their POSITION/size comes from the group.
    struct UILayoutGroupData
    {
        REFLECT()

        PROPERTY( DisplayName( "Type" ), Category( "UI Layout Group" ) )
        UILayoutType Type = UILayoutType::Vertical;

        PROPERTY( DisplayName( "Padding L/T/R/B" ), Category( "UI Layout Group" ) )
        glm::vec4 Padding = glm::vec4( 8.0f );

        PROPERTY( DisplayName( "Spacing" ), Category( "UI Layout Group" ), Range( 0.0f, 128.0f ) )
        float Spacing = 6.0f;

        PROPERTY( DisplayName( "Stretch Children (cross axis)" ), Category( "UI Layout Group" ) )
        bool StretchCross = true;

        PROPERTY( DisplayName( "Grid Cell Size" ), Category( "UI Layout Group" ) )
        glm::vec2 CellSize = glm::vec2( 100.0f, 100.0f );

        PROPERTY( DisplayName( "Grid Columns (0 = auto)" ), Category( "UI Layout Group" ), Range( 0.0f, 64.0f ) )
        int Columns = 0;
    };
    struct UILayoutGroupComponent
    {
        UILayoutGroupData Data;
    };

    // A horizontal progress/health bar: a background track with a fill spanning Value (0..1) of the width.
    // Display-only (no interaction).
    struct UIProgressBarData
    {
        REFLECT()

        PROPERTY( DisplayName( "Value" ), Category( "UI Progress Bar" ), Range( 0.0f, 1.0f ) )
        float Value = 0.5f;

        PROPERTY( DisplayName( "Background" ), Category( "UI Progress Bar" ), Color )
        glm::vec3 Background = glm::vec3( 0.12f, 0.13f, 0.16f );

        PROPERTY( DisplayName( "Fill" ), Category( "UI Progress Bar" ), Color )
        glm::vec3 Fill = glm::vec3( 0.30f, 0.65f, 0.35f );

        PROPERTY( DisplayName( "Corner Radius" ), Category( "UI Progress Bar" ), Range( 0.0f, 32.0f ) )
        float CornerRadius = 4.0f;
    };
    struct UIProgressBarComponent
    {
        UIProgressBarData Data;
    };

    // A checkbox: a box that fills with the check colour when on. A click (runtime) flips Value.
    struct UIToggleData
    {
        REFLECT()

        PROPERTY( DisplayName( "Value (on)" ), Category( "UI Toggle" ) )
        bool Value = false;

        PROPERTY( DisplayName( "Box Color" ), Category( "UI Toggle" ), Color )
        glm::vec3 BoxColor = glm::vec3( 0.18f, 0.19f, 0.24f );

        PROPERTY( DisplayName( "Check Color" ), Category( "UI Toggle" ), Color )
        glm::vec3 CheckColor = glm::vec3( 0.30f, 0.60f, 0.90f );

        PROPERTY( DisplayName( "Corner Radius" ), Category( "UI Toggle" ), Range( 0.0f, 32.0f ) )
        float CornerRadius = 4.0f;
    };
    struct UIToggleComponent
    {
        UIToggleData Data;
    };

    // A horizontal slider: a track + a fill up to the handle + a draggable handle. Dragging (runtime) sets
    // Value in [MinValue, MaxValue].
    struct UISliderData
    {
        REFLECT()

        PROPERTY( DisplayName( "Value" ), Category( "UI Slider" ) )
        float Value = 0.5f;

        PROPERTY( DisplayName( "Min" ), Category( "UI Slider" ) )
        float MinValue = 0.0f;

        PROPERTY( DisplayName( "Max" ), Category( "UI Slider" ) )
        float MaxValue = 1.0f;

        PROPERTY( DisplayName( "Track Color" ), Category( "UI Slider" ), Color )
        glm::vec3 TrackColor = glm::vec3( 0.12f, 0.13f, 0.16f );

        PROPERTY( DisplayName( "Fill Color" ), Category( "UI Slider" ), Color )
        glm::vec3 FillColor = glm::vec3( 0.30f, 0.52f, 0.82f );

        PROPERTY( DisplayName( "Handle Color" ), Category( "UI Slider" ), Color )
        glm::vec3 HandleColor = glm::vec3( 0.90f, 0.92f, 0.96f );
    };
    struct UISliderComponent
    {
        UISliderData Data;
    };

    // A vertical scroll view: clips its children to its rect and scrolls them by the mouse wheel. Children are
    // positioned relative to the content top (offset up by ScrollY). ContentHeight is the total scrollable
    // height in design px (author-set); scrolling clamps to [0, ContentHeight - viewport height].
    struct UIScrollViewData
    {
        REFLECT()

        PROPERTY( DisplayName( "Scroll Y" ), Category( "UI Scroll View" ) )
        float ScrollY = 0.0f;

        PROPERTY( DisplayName( "Content Height" ), Category( "UI Scroll View" ) )
        float ContentHeight = 600.0f;

        PROPERTY( DisplayName( "Background" ), Category( "UI Scroll View" ), Color )
        glm::vec3 Background = glm::vec3( 0.10f, 0.11f, 0.14f );

        PROPERTY( DisplayName( "Show Scrollbar" ), Category( "UI Scroll View" ) )
        bool ShowScrollbar = true;

        PROPERTY( DisplayName( "Scrollbar Color" ), Category( "UI Scroll View" ), Color )
        glm::vec3 ScrollbarColor = glm::vec3( 0.35f, 0.37f, 0.44f );
    };
    struct UIScrollViewComponent
    {
        UIScrollViewData Data;
    };

    // A single-line text input. Click to focus (runtime), then typing edits Text; a caret shows at the end.
    // Placeholder shows (dimmed) when empty + unfocused.
    struct UIInputFieldData
    {
        REFLECT()

        PROPERTY( DisplayName( "Text" ), Category( "UI Input Field" ) )
        std::string Text;

        PROPERTY( DisplayName( "Placeholder" ), Category( "UI Input Field" ) )
        std::string Placeholder = "Enter text...";

        PROPERTY( DisplayName( "Font Size" ), Category( "UI Input Field" ), Range( 6.0f, 96.0f ) )
        float FontSize = 20.0f;

        PROPERTY( DisplayName( "Text Color" ), Category( "UI Input Field" ), Color )
        glm::vec3 TextColor = glm::vec3( 0.92f, 0.94f, 0.98f );

        PROPERTY( DisplayName( "Placeholder Color" ), Category( "UI Input Field" ), Color )
        glm::vec3 PlaceholderColor = glm::vec3( 0.45f, 0.47f, 0.52f );

        PROPERTY( DisplayName( "Background" ), Category( "UI Input Field" ), Color )
        glm::vec3 Background = glm::vec3( 0.10f, 0.11f, 0.14f );

        PROPERTY( DisplayName( "Focus Border" ), Category( "UI Input Field" ), Color )
        glm::vec3 FocusColor = glm::vec3( 0.30f, 0.55f, 0.90f );

        PROPERTY( DisplayName( "Corner Radius" ), Category( "UI Input Field" ), Range( 0.0f, 32.0f ) )
        float CornerRadius = 4.0f;
    };
    struct UIInputFieldComponent
    {
        UIInputFieldData Data;
    };

    // A dropdown / combo box. Shows the selected option; a click opens a list of Options (';'-separated) below
    // it, drawn on top of everything. Picking an option sets SelectedIndex and closes.
    struct UIDropdownData
    {
        REFLECT()

        PROPERTY( DisplayName( "Options (';'-separated)" ), Category( "UI Dropdown" ) )
        std::string Options = "Option A;Option B;Option C";

        PROPERTY( DisplayName( "Selected Index" ), Category( "UI Dropdown" ) )
        int SelectedIndex = 0;

        PROPERTY( DisplayName( "Open" ), Category( "UI Dropdown" ) )
        bool Open = false;

        PROPERTY( DisplayName( "Font Size" ), Category( "UI Dropdown" ), Range( 6.0f, 96.0f ) )
        float FontSize = 20.0f;

        PROPERTY( DisplayName( "Background" ), Category( "UI Dropdown" ), Color )
        glm::vec3 Background = glm::vec3( 0.16f, 0.17f, 0.21f );

        PROPERTY( DisplayName( "Text Color" ), Category( "UI Dropdown" ), Color )
        glm::vec3 TextColor = glm::vec3( 0.92f, 0.94f, 0.98f );

        PROPERTY( DisplayName( "Highlight" ), Category( "UI Dropdown" ), Color )
        glm::vec3 Highlight = glm::vec3( 0.26f, 0.40f, 0.62f );

        PROPERTY( DisplayName( "Corner Radius" ), Category( "UI Dropdown" ), Range( 0.0f, 32.0f ) )
        float CornerRadius = 4.0f;
    };
    struct UIDropdownComponent
    {
        UIDropdownData Data;
    };

    // Root of a screen-space UI tree. Child entities with a UILayout are laid out against this canvas. Add UI
    // elements as CHILDREN of the canvas entity (the viewport "UI" menu / UI Editor do this for you).
    struct UICanvasData
    {
        REFLECT()

        PROPERTY( DisplayName( "Scale Mode" ), Category( "UI Canvas" ) )
        UICanvasScaleMode ScaleMode = UICanvasScaleMode::Stretch;

        PROPERTY( DisplayName( "Render Mode" ), Category( "UI Canvas" ) )
        UICanvasRenderMode RenderMode = UICanvasRenderMode::ScreenSpace;

        PROPERTY( DisplayName( "World Scale" ), Category( "UI Canvas" ), Range( 1.0f, 4000.0f ) )
        float WorldScale = 400.0f; // WorldSpace: on-screen px per reference-unit at distance 1

        PROPERTY( DisplayName( "Reference Width" ), Category( "UI Canvas" ), Range( 64.0f, 7680.0f ) )
        float ReferenceWidth = 1280.0f;

        PROPERTY( DisplayName( "Reference Height" ), Category( "UI Canvas" ), Range( 64.0f, 4320.0f ) )
        float ReferenceHeight = 720.0f;

        PROPERTY( DisplayName( "Match Width/Height" ), Category( "UI Canvas" ), Range( 0.0f, 1.0f ) )
        float MatchWidthHeight = 0.5f; // ScaleWithScreen only: 0 = match width, 1 = match height

        PROPERTY( DisplayName( "Background Sprite" ), Category( "UI Canvas" ) )
        Assets::AssetHandle Sprite; // optional full-canvas background image (unset = transparent)

        PROPERTY( DisplayName( "Visible" ), Category( "UI Canvas" ) )
        bool Visible = true;
    };
    struct UICanvasComponent
    {
        UICanvasData Data;
    };

    // Godot Control-like rect: anchors (fraction of the parent rect, 0..1), offsets (pixels from the anchored
    // edges), a custom minimum size, a pivot and content clipping. The layout solver turns these into a screen
    // rect each frame. AnchorMin==AnchorMax => fixed-size element positioned by offsets; spread anchors =>
    // element stretches with the parent.
    struct UILayoutData
    {
        REFLECT()

        PROPERTY( DisplayName( "Anchor Min" ), Category( "UI Layout" ) )
        glm::vec2 AnchorMin = glm::vec2( 0.0f, 0.0f );

        PROPERTY( DisplayName( "Anchor Max" ), Category( "UI Layout" ) )
        glm::vec2 AnchorMax = glm::vec2( 0.0f, 0.0f );

        PROPERTY( DisplayName( "Offset Min" ), Category( "UI Layout" ) )
        glm::vec2 OffsetMin = glm::vec2( 0.0f, 0.0f ); // px from the AnchorMin edges (left/top)

        PROPERTY( DisplayName( "Offset Max" ), Category( "UI Layout" ) )
        glm::vec2 OffsetMax = glm::vec2( 160.0f, 48.0f ); // px from the AnchorMax edges (right/bottom)

        PROPERTY( DisplayName( "Custom Minimum Size" ), Category( "UI Layout" ) )
        glm::vec2 CustomMinimumSize = glm::vec2( 0.0f, 0.0f );

        PROPERTY( DisplayName( "Pivot" ), Category( "UI Layout" ) )
        glm::vec2 Pivot = glm::vec2( 0.5f, 0.5f );

        PROPERTY( DisplayName( "Clip Contents" ), Category( "UI Layout" ) )
        bool ClipContents = false;
    };
    struct UILayoutComponent
    {
        UILayoutData Data;
    };

    // A filled (rounded) rectangle — the background of a panel / window / button.
    struct UIPanelData
    {
        REFLECT()

        PROPERTY( DisplayName( "Color" ), Category( "UI Panel" ), Color )
        glm::vec3 Color = glm::vec3( 0.15f, 0.16f, 0.2f );

        PROPERTY( DisplayName( "Opacity" ), Category( "UI Panel" ), Range( 0.0f, 1.0f ) )
        float Opacity = 0.92f;

        PROPERTY( DisplayName( "Corner Radius" ), Category( "UI Panel" ), Range( 0.0f, 64.0f ) )
        float CornerRadius = 6.0f;

        PROPERTY( DisplayName( "Sprite" ), Category( "UI Panel" ) )
        Assets::AssetHandle Sprite; // optional background image, tinted by Color * Opacity. Unset = flat colour.

        PROPERTY( DisplayName( "Sprite Border L/T/R/B" ), Category( "UI Panel" ) )
        glm::vec4 SpriteBorder = glm::vec4( 0.0f ); // 9-slice: source-px borders kept unstretched (0 = stretch)

        PROPERTY( DisplayName( "Video Path (.mpg)" ), Category( "UI Panel" ) )
        std::string VideoPath; // MPEG1 .mpg/.mpeg streamed into this panel (loops, tinted by Color*Opacity).
                               // Overrides the sprite/gradient fill while set. Empty = no video.

        // --- Shape (Phase C) ------------------------------------------------------------------------------
        PROPERTY( DisplayName( "Circle" ), Category( "UI Panel" ) )
        bool Circle = false; // force a perfect circle/ellipse (rounding = half the shorter side) at any size —
                             // for avatars, badges, status dots. Overrides Corner Radius.

        PROPERTY( DisplayName( "Ring Width" ), Category( "Ring" ), Range( 0.0f, 24.0f ) )
        float RingWidth = 0.0f; // >0 draws a gradient ring hugging the (circular or rounded) edge
        PROPERTY( DisplayName( "Ring Color A" ), Category( "Ring" ), Color )
        glm::vec3 RingColorA = glm::vec3( 1.0f, 0.48f, 0.15f );
        PROPERTY( DisplayName( "Ring Color B" ), Category( "Ring" ), Color )
        glm::vec3 RingColorB = glm::vec3( 0.18f, 0.89f, 1.0f ); // sweeps A -> B -> A around the ring

        // Effects (Phase 4). All in design px; scaled by the canvas scale at draw time.
        PROPERTY( DisplayName( "Use Gradient" ), Category( "Effects" ) )
        bool UseGradient = false; // vertical Color (top) -> Gradient Color (bottom); ignored when a sprite is set
        PROPERTY( DisplayName( "Gradient Color" ), Category( "Effects" ), Color )
        glm::vec3 GradientColor = glm::vec3( 0.10f, 0.11f, 0.14f );
        PROPERTY( DisplayName( "Border Width" ), Category( "Effects" ), Range( 0.0f, 16.0f ) )
        float BorderWidth = 0.0f; // 0 = no border
        PROPERTY( DisplayName( "Border Color" ), Category( "Effects" ), Color )
        glm::vec3 BorderColor = glm::vec3( 0.0f );
        PROPERTY( DisplayName( "Shadow" ), Category( "Effects" ) )
        bool Shadow = false;
        PROPERTY( DisplayName( "Shadow Color" ), Category( "Effects" ), Color )
        glm::vec3 ShadowColor = glm::vec3( 0.0f );
        PROPERTY( DisplayName( "Shadow Offset" ), Category( "Effects" ) )
        glm::vec2 ShadowOffset = glm::vec2( 3.0f, 4.0f );
        PROPERTY( DisplayName( "Glow" ), Category( "Effects" ) )
        bool Glow = false; // cheap ImDrawList glow: layered expanded rects behind, alpha falloff
        PROPERTY( DisplayName( "Glow Color" ), Category( "Effects" ), Color )
        glm::vec3 GlowColor = glm::vec3( 0.3f, 0.6f, 1.0f );
        PROPERTY( DisplayName( "Glow Size" ), Category( "Effects" ), Range( 0.0f, 48.0f ) )
        float GlowSize = 12.0f;
    };
    struct UIPanelComponent
    {
        UIPanelData Data;
    };

    // Screen-space text label (distinct from the 3D world-space TextComponent).
    struct UITextData
    {
        REFLECT()

        PROPERTY( DisplayName( "Text" ), Category( "UI Text" ) )
        std::string Text = "Label";

        PROPERTY( DisplayName( "Font Size" ), Category( "UI Text" ), Range( 6.0f, 200.0f ) )
        float FontSize = 22.0f;

        PROPERTY( DisplayName( "Font" ), Category( "UI Text" ), Asset<FontAsset> )
        Assets::AssetHandle Font; // SDF font asset — drag a .ttf from the Content Browser or pick a preloaded
                                  // one. Unset = the engine's built-in default (Roboto).

        PROPERTY( DisplayName( "Color" ), Category( "UI Text" ), Color )
        glm::vec3 Color = glm::vec3( 1.0f, 1.0f, 1.0f );

        PROPERTY( DisplayName( "Alignment" ), Category( "UI Text" ) )
        UITextAlign Align = UITextAlign::Center;

        PROPERTY( DisplayName( "Vertical Align" ), Category( "UI Text" ) )
        UITextVAlign VerticalAlign = UITextVAlign::Middle;

        // --- Layout (Phase E) ---------------------------------------------------------------------------
        PROPERTY( DisplayName( "Word Wrap" ), Category( "Layout" ) )
        bool Wrap = false; // break long lines at word boundaries to fit the element width

        PROPERTY( DisplayName( "Line Spacing" ), Category( "Layout" ), Range( 0.5f, 3.0f ) )
        float LineSpacing = 1.0f; // multiplier on the font's natural line height

        PROPERTY( DisplayName( "Auto Size" ), Category( "Layout" ) )
        bool AutoSize = false; // shrink the font (down to Min Font Size) until the block fits the rect

        PROPERTY( DisplayName( "Min Font Size" ), Category( "Layout" ), Range( 4.0f, 200.0f ) )
        float MinFontSize = 8.0f; // floor for Auto Size

        PROPERTY( DisplayName( "Overflow" ), Category( "Layout" ) )
        UITextOverflow Overflow = UITextOverflow::Overflow; // what to do when the text still doesn't fit

        PROPERTY( DisplayName( "Rich Text" ), Category( "Layout" ) )
        bool RichText = false; // parse BBCode tags: [color=#rrggbb]..[/color], [b]..[/b]

        // Effects (Phase 4).
        PROPERTY( DisplayName( "Shadow" ), Category( "Effects" ) )
        bool Shadow = false;
        PROPERTY( DisplayName( "Shadow Color" ), Category( "Effects" ), Color )
        glm::vec3 ShadowColor = glm::vec3( 0.0f );
        PROPERTY( DisplayName( "Shadow Offset" ), Category( "Effects" ) )
        glm::vec2 ShadowOffset = glm::vec2( 1.0f, 1.0f );
        PROPERTY( DisplayName( "Outline" ), Category( "Effects" ) )
        bool Outline = false;
        PROPERTY( DisplayName( "Outline Color" ), Category( "Effects" ), Color )
        glm::vec3 OutlineColor = glm::vec3( 0.0f );
    };
    struct UITextComponent2D
    {
        UITextData Data;
    };

    // Interactive button: tints its panel by pointer state and dispatches OnClickMessage to Lua on click.
    // What a UI Button does when clicked. The target/payload is the button's "Action Target" string:
    //  LoadScene   -> load that scene path        SendMessage -> gameplay event name (Lua/scripts)
    //  QuitGame    -> quit (target ignored)        OpenURL     -> open the URL
    enum class UIButtonAction
    {
        None,
        SendMessage,
        LoadScene,
        QuitGame,
        OpenURL
    };

    struct UIButtonData
    {
        REFLECT()

        PROPERTY( DisplayName( "Normal" ), Category( "UI Button" ), Color )
        glm::vec3 NormalColor = glm::vec3( 0.20f, 0.40f, 0.70f );

        PROPERTY( DisplayName( "Hover" ), Category( "UI Button" ), Color )
        glm::vec3 HoverColor = glm::vec3( 0.30f, 0.52f, 0.82f );

        PROPERTY( DisplayName( "Pressed" ), Category( "UI Button" ), Color )
        glm::vec3 PressedColor = glm::vec3( 0.15f, 0.30f, 0.55f );

        PROPERTY( DisplayName( "Click Action" ), Category( "UI Button" ) )
        UIButtonAction Action = UIButtonAction::SendMessage;

        PROPERTY( DisplayName( "Action Target" ), Category( "UI Button" ) )
        std::string OnClickMessage = ""; // scene path / message name / URL, depending on Action

        PROPERTY( DisplayName( "Sprite" ), Category( "UI Button" ) )
        Assets::AssetHandle Sprite; // normal-state image, tinted by the state colour. Unset = flat colour.

        PROPERTY( DisplayName( "Hover Sprite" ), Category( "UI Button" ) )
        Assets::AssetHandle HoverSprite; // shown on hover (falls back to Sprite if unset)

        PROPERTY( DisplayName( "Pressed Sprite" ), Category( "UI Button" ) )
        Assets::AssetHandle PressedSprite; // shown while pressed (falls back to Sprite if unset)

        PROPERTY( DisplayName( "Sprite Border L/T/R/B" ), Category( "UI Button" ) )
        glm::vec4 SpriteBorder = glm::vec4( 0.0f ); // 9-slice: source-px borders kept unstretched (0 = stretch)

        // --- States (Phase D) -----------------------------------------------------------------------------
        PROPERTY( DisplayName( "Selected" ), Category( "State" ) )
        bool Selected = false; // persistent highlight (active menu item / current tab): rests on SelectedColor
                               // and draws an accent bar, until hover/press temporarily override it
        PROPERTY( DisplayName( "Selected Color" ), Category( "State" ), Color )
        glm::vec3 SelectedColor = glm::vec3( 0.85f, 0.42f, 0.18f );
        PROPERTY( DisplayName( "Selected Accent" ), Category( "State" ), Color )
        glm::vec3 SelectedAccent = glm::vec3( 1.0f, 0.55f, 0.2f ); // the left accent bar / ring colour

        PROPERTY( DisplayName( "Disabled" ), Category( "State" ) )
        bool Disabled = false; // greyed + non-interactive (ignores hover/press/click)
        PROPERTY( DisplayName( "Disabled Color" ), Category( "State" ), Color )
        glm::vec3 DisabledColor = glm::vec3( 0.22f, 0.24f, 0.28f );
    };
    struct UIButtonComponent
    {
        UIButtonData Data;
    };

    // Reflected (REFLECT/PROPERTY) so it (de)serializes generically — the SkyboxHandle round-trips as an
    // asset PATH via the serializer's AssetResolver. RequestBake has NO PROPERTY → excluded from
    // reflection (transient). All fields kept flat (no Data sub-struct) so existing accessors are unchanged.
    struct SkyboxComponent
    {
        REFLECT()

        // Hidden from the auto-generated Details (the widget draws a proper SkyboxAsset picker + DnD instead
        // of the builder's texture-oriented asset slot). Still serialized — Hidden is editor-only.
        PROPERTY( DisplayName( "Skybox" ), Category( "Skybox" ), Asset<SkyboxAsset>, Hidden )
        Assets::AssetHandle SkyboxHandle;

        PROPERTY( DisplayName( "Intensity" ), Category( "Skybox" ), Range( 0.0f, 10.0f ) )
        float Intensity = 1.0f;

        // Engine-generated procedural atmosphere (Rayleigh+Mie).
        PROPERTY( DisplayName( "Procedural" ), Category( "Skybox" ) )
        bool Procedural = false;
        PROPERTY( DisplayName( "Sun Intensity" ), Category( "Skybox" ), Range( 1.0f, 50.0f ) )
        float SunIntensity = 22.0f; // atmosphere sun radiance scale
        PROPERTY( DisplayName( "Sun Disk Size" ), Category( "Skybox" ), Range( 0.002f, 0.1f ) )
        float SunDiskRadius = 0.02f; // sun angular radius (radians)

        // --- Artistic sky palette + scalars (the day/sunset/night look is driven by the sun elevation; these
        // tune the colours/intensities). See ProceduralSky shader / Atmosphere.glslh SkyConfig. ---
        PROPERTY( DisplayName( "Zenith Color" ), Category( "Sky Color" ), Color )
        glm::vec3 ZenithColor = { 0.08f, 0.26f, 0.70f };
        PROPERTY( DisplayName( "Horizon Color" ), Category( "Sky Color" ), Color )
        glm::vec3 HorizonColor = { 0.50f, 0.66f, 0.92f };
        PROPERTY( DisplayName( "Ground Color" ), Category( "Sky Color" ), Color )
        glm::vec3 GroundColor = { 0.16f, 0.19f, 0.24f };
        PROPERTY( DisplayName( "Night Color" ), Category( "Sky Color" ), Color )
        glm::vec3 NightColor = { 0.010f, 0.020f, 0.050f };
        PROPERTY( DisplayName( "Sky Brightness" ), Category( "Sky Color" ), Range( 0.0f, 4.0f ) )
        float SkyBrightness = 1.0f;
        PROPERTY( DisplayName( "Horizon Falloff" ), Category( "Sky Color" ), Range( 0.1f, 2.0f ) )
        float HorizonFalloff = 0.85f;

        PROPERTY( DisplayName( "Sun Color" ), Category( "Sun & Sky" ), Color )
        glm::vec3 SunColor = { 1.00f, 0.96f, 0.88f };
        PROPERTY( DisplayName( "Sun Glow" ), Category( "Sun & Sky" ), Range( 0.0f, 5.0f ) )
        float SunGlow = 1.0f;
        PROPERTY( DisplayName( "Sunset Color" ), Category( "Sun & Sky" ), Color )
        glm::vec3 SunsetColor = { 1.00f, 0.42f, 0.18f };
        PROPERTY( DisplayName( "Sunset Intensity" ), Category( "Sun & Sky" ), Range( 0.0f, 3.0f ) )
        float SunsetIntensity = 1.0f;
        PROPERTY( DisplayName( "Star Intensity" ), Category( "Sun & Sky" ), Range( 0.0f, 5.0f ) )
        float StarIntensity = 1.0f;

        // Procedural flat-layer clouds (e2gamedev-style; painted in the sky shader, visual only).
        PROPERTY( DisplayName( "Clouds" ), Category( "Clouds" ) )
        bool EnableClouds = false;
        PROPERTY( DisplayName( "Coverage" ), Category( "Clouds" ), Range( 0.0f, 1.0f ) )
        float CloudCoverage = 0.5f; // 0 = clear sky, 1 = overcast
        PROPERTY( DisplayName( "Density" ), Category( "Clouds" ), Range( 0.0f, 2.0f ) )
        float CloudDensity = 1.0f; // opacity multiplier
        PROPERTY( DisplayName( "Tiling" ), Category( "Clouds" ), Range( 0.2f, 10.0f ) )
        float CloudTiling = 1.5f; // cloud scale (bigger = smaller cells)
        PROPERTY( DisplayName( "Brightness" ), Category( "Clouds" ), Range( 0.0f, 3.0f ) )
        float CloudBrightness = 1.0f; // cloud albedo multiplier
        PROPERTY( DisplayName( "Wind Speed" ), Category( "Clouds" ), Range( 0.0f, 50.0f ) )
        float CloudWindSpeed = 8.0f; // horizontal drift speed (animation)

        // Transient (not serialized — no PROPERTY): set by the editor's "Bake" button.
        bool RequestBake = false;
    };

    // Scene-outliner grouping node: an otherwise-empty entity that acts as a FOLDER for organizing the
    // hierarchy (drag entities under it). It renders nothing — just parents children via RelationshipComponent
    // — so it's purely an authoring aid. Marker component (no data); serialized so folders persist.
    struct FolderComponent
    {
    };

    struct PrefabComponent
    {
        Assets::AssetHandle Prefab;
    };

    struct RelationshipComponent
    {
        entt::entity              Parent = entt::null;
        std::vector<entt::entity> Children;
    };

    // --- Physics (Jolt) ---------------------------------------------------------------------------------
    // A body's collision SHAPE (reflected -> Details UI + serialization). Paired with RigidBodyComponent
    // to be simulated by PhysicsECSSystem.
    struct ColliderData
    {
        REFLECT()

        PROPERTY( DisplayName( "Shape" ), Category( "Collider" ) )
        Physics::ShapeType Shape = Physics::ShapeType::Box;

        PROPERTY( DisplayName( "Half Extents" ), Category( "Collider" ) )
        glm::vec3 HalfExtents = { 0.5f, 0.5f, 0.5f }; // Box

        PROPERTY( DisplayName( "Radius" ), Category( "Collider" ), Range( 0.01f, 50.0f ) )
        float Radius = 0.5f; // Sphere / Capsule

        PROPERTY( DisplayName( "Half Height" ), Category( "Collider" ), Range( 0.01f, 50.0f ) )
        float HalfHeight = 0.5f; // Capsule
    };

    struct ColliderComponent
    {
        ColliderData Data;
    };

    // --- Audio (miniaudio) ------------------------------------------------------------------------------
    // A sound emitter (reflected -> Details UI + serialization). AudioECSSystem creates the runtime
    // source in Play mode (AutoPlay), positions spatial sources at the entity's world transform, and
    // stops everything on the Play->Edit transition.
    struct AudioSourceData
    {
        REFLECT()

        PROPERTY( DisplayName( "Clip" ), Category( "Audio" ) )
        std::string Clip; // audio file (wav/mp3/flac), absolute or Assets-relative

        PROPERTY( DisplayName( "Volume" ), Category( "Audio" ), Range( 0.0f, 2.0f ) )
        float Volume = 1.0f;

        PROPERTY( DisplayName( "Loop" ), Category( "Audio" ) )
        bool Loop = false;

        PROPERTY( DisplayName( "Auto Play" ), Category( "Audio" ) )
        bool AutoPlay = true; // start when the scene enters Play

        PROPERTY( DisplayName( "3D Spatial" ), Category( "Audio" ) )
        bool Spatial = true; // attenuate/pan from the entity position vs. the listener (camera)
    };

    struct AudioSourceComponent
    {
        AudioSourceData Data;
    };

    // Body simulation params (reflected -> Details UI + serialization).
    struct RigidBodyData
    {
        REFLECT()

        PROPERTY( DisplayName( "Type" ), Category( "Rigid Body" ) )
        Physics::BodyType Type = Physics::BodyType::Dynamic;

        PROPERTY( DisplayName( "Mass" ), Category( "Rigid Body" ), Range( 0.0f, 1000.0f ) )
        float Mass = 1.0f;

        PROPERTY( DisplayName( "Friction" ), Category( "Rigid Body" ), Range( 0.0f, 2.0f ) )
        float Friction = 0.5f;

        PROPERTY( DisplayName( "Restitution" ), Category( "Rigid Body" ), Range( 0.0f, 1.0f ) )
        float Restitution = 0.1f;
    };

    // Marks an entity as a physics body. Static = immovable, Dynamic = simulated, Kinematic = code-driven.
    struct RigidBodyComponent
    {
        RigidBodyData Data;

        // Transient: the live Jolt body (created on Play, cleared on Stop). Not reflected/serialized.
        Physics::BodyHandle RuntimeBody = Physics::kInvalidBody;
    };

    // Playable character controller params (reflected -> Details UI + serialization). Drives a Jolt
    // CharacterVirtual capsule (walks slopes/steps, blocked by world geometry) via WASD + jump.
    struct CharacterControllerData
    {
        REFLECT()

        // PHYSICS / capsule only. The control FEEL (move/sprint/look/jump speed) lives in the controller
        // SCRIPT's Properties — not here — so there's a single source of truth for behavior. See ScriptComponent.
        PROPERTY( DisplayName( "Radius" ), Category( "Character" ), Range( 0.05f, 5.0f ) )
        float Radius = 0.3f;

        PROPERTY( DisplayName( "Height" ), Category( "Character" ), Range( 0.2f, 10.0f ) )
        float Height = 1.8f; // total capsule height (HalfHeight = (Height - 2*Radius) / 2)

        PROPERTY( DisplayName( "Max Slope" ), Category( "Character" ), Range( 0.0f, 89.0f ) )
        float MaxSlopeDeg = 50.0f;

        // Fall acceleration (m/s^2). Default ~2x real gravity so the jump arc feels SNAPPY (real 9.81 reads as
        // floaty). Authorable per-character instead of a baked engine constant — a moon level just lowers it.
        PROPERTY( DisplayName( "Gravity" ), Category( "Character" ), Range( 0.0f, 60.0f ) )
        float Gravity = 20.0f;
    };

    // A WASD-driven player. The follow camera is NOT here — parent a child entity with a CameraComponent
    // (offset behind = 3rd person, at the head = 1st person); it tracks the player via the hierarchy.
    struct CharacterControllerComponent
    {
        CharacterControllerData Data;

        // Transient (Play only): the live Jolt character + the integrated vertical velocity (gravity/jump).
        Physics::CharacterHandle RuntimeCharacter = Physics::kInvalidCharacter;
        float                    VerticalVelocity = 0.0f;
        float                    CurrentSpeed     = 0.0f; // planar move speed this frame (drives locomotion anim)

        // Move INTENT, set by the controller SCRIPT each frame (the engine only executes the physics). This is
        // the mechanism/behavior split: the script reads input + decides where to go; PhysicsECSSystem turns
        // this into a camera-relative velocity and steps Jolt.
        glm::vec2 MoveInput     = { 0.0f, 0.0f }; // x = strafe (right), y = forward; each -1..1
        float     DesiredSpeed  = 0.0f;           // m/s the script asked for (sprint etc. is script policy)
        bool      JumpRequested = false;          // set by script:jump(strength), consumed + cleared by physics
        float     JumpStrength  = 5.0f;           // launch velocity the script passed to self:jump()
        bool      OnGround      = false;          // last physics result, exposed to scripts (self:isOnGround())
        glm::vec2 AirVelocity   = { 0.0f, 0.0f }; // horizontal velocity locked at takeoff (no air control)

        // Swimming (set by the controller SCRIPT when it detects the body is below the water level). While
        // swimming, PhysicsECSSystem replaces gravity with buoyancy, gives full 3D control, and drives the
        // vertical from SwimVertical (+1 = up, -1 = down) instead of jump/gravity.
        bool  Swimming     = false;
        float SwimVertical = 0.0f; // -1..1 swim up/down intent (script)
    };

    // Attaches a Lua script to an entity. The ScriptSystem loads the file and calls its OnStart()/OnUpdate(dt);
    // the script drives behavior through the bound API (self:move/jump/addYaw..., Input.*). See ScriptEngine.
    // A behavior unit, like a UE ActorComponent: one .lua file + its exposed properties + lifecycle flag.
    struct ScriptSlot
    {
        std::string ScriptPath; // .lua file, relative to the working dir (e.g. "Resources/Scripts/x.lua")

        // Editor-exposed properties (from the script's `Properties` table): per-entity values, edited in
        // Details and serialized. Written into the script env before it runs (so the script reads them).
        std::vector<Scripting::ScriptProperty> Properties;

        bool Started = false; // transient: OnStart already called for this instance
    };

    // One entity can run MANY scripts (EnTT allows only one component of a type per entity, so multiple
    // behaviors live as a LIST of slots inside this one component — the same composition UE gets from
    // multiple ActorComponents). Each slot is an independent sandbox (its own env + properties + lifecycle);
    // all slots share the same `self` entity. ScriptSystem ticks every slot; entity:call() broadcasts to all.
    struct ScriptComponent
    {
        std::vector<ScriptSlot> Scripts;
    };

    // UE-style SOCKET attachment: makes this entity follow a BONE of another (skinned) entity, not just its
    // root. The hand is a bone inside a Skeleton — it has no entity, so RelationshipComponent can't parent to
    // it. AttachmentSystem (after AnimationECSSystem) computes the bone's world transform from the target's
    // animator pose and writes it (plus the local offset) into this entity's TransformComponent each frame.
    // Used for weapons-in-hand, hats, backpacks, scope attachments, etc.
    struct SocketAttachmentComponent
    {
        Common::UUID Target;   // the skinned-mesh entity whose bone we follow (invalid = detached)
        std::string  BoneName; // bone/socket name on the target's skeleton (e.g. "mixamorig:RightHand")

        // Grip alignment relative to the bone (the weapon almost never sits exactly on the bone origin).
        glm::vec3 OffsetTranslation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 OffsetRotation    = { 0.0f, 0.0f, 0.0f }; // euler radians
        glm::vec3 OffsetScale       = { 1.0f, 1.0f, 1.0f };
    };

    // A flying projectile (bullet/grenade/arrow): integrated each frame by ProjectileSystem (Play only). On a
    // swept hit it delivers "OnHit" to the struck entity's script and is destroyed; it also dies on lifetime.
    // Mechanism (movement + collision) is C++; the DECISION to fire + what a hit MEANS stay in Lua.
    struct ProjectileComponent
    {
        glm::vec3    Velocity      = { 0.0f, 0.0f, 0.0f }; // world units/s
        float        GravityScale  = 0.0f;                 // 0 = straight line, 1 = full gravity (arc)
        float        LifeRemaining = 5.0f;                 // seconds before auto-despawn
        float        Damage        = 10.0f;
        Common::UUID Owner; // shooter (so we can skip self-hits)
    };
} // namespace Desert::ECS