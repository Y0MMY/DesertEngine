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

#include <Engine/Reflection/ReflectionMacros.hpp>

namespace Desert
{
    class Mesh;
    class DynamicMesh;
}

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
    // to its GPU representation. This is the general concept — a surface Material (PBRMaterialData) is
    // just ONE specialization; camera and lights are others. NOT a material, hence the member is `Data`.
    struct CameraData
    {
        REFLECT()

        PROPERTY( DisplayName( "Main Camera" ), Category( "Camera" ) )
        bool IsMainCamera = true;

        PROPERTY( DisplayName( "Field of View" ), Category( "Camera" ), Range( 10.0f, 120.0f ) )
        float FOV = 45.0f;

        PROPERTY( DisplayName( "Near" ), Category( "Camera" ), Range( 0.01f, 10.0f ) )
        float Near = 0.1f;

        PROPERTY( DisplayName( "Far" ), Category( "Camera" ), Range( 10.0f, 10000.0f ) )
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
        Assets::AssetHandle                       MeshHandle;
        std::vector<Assets::AssetHandle>          MaterialSlots;
        std::vector<Graphic::MaterialInstancePtr> RuntimeMaterialInstances; // Cache to keep instances alive and avoid per-frame allocations
        std::optional<Geometry::PrimitiveType>    Primitive;                // Optional primitive type for dynamic generation
        std::shared_ptr<DynamicMesh>              RuntimeMesh;              // Unique mesh instance for modifications
        bool                                      OutlineDraw = false;
    };

    struct SkinnedMeshComponent
    {
        Assets::AssetHandle                       MeshHandle;
        std::vector<Assets::AssetHandle>          MaterialSlots;
        std::vector<Graphic::MaterialInstancePtr> RuntimeMaterialInstances; // Cache to keep instances alive and avoid per-frame allocations
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
        static constexpr uint32_t        SplatResolution = 256;
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

        AnimationComponent() = default;

        explicit AnimationComponent( std::unique_ptr<Animation::Animator>&& animator )
             : Animator( std::move( animator ) )
        {
        }
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

    // Reflected (REFLECT/PROPERTY) so it (de)serializes generically — the SkyboxHandle round-trips as an
    // asset PATH via the serializer's AssetResolver. RequestBake has NO PROPERTY → excluded from
    // reflection (transient). All fields kept flat (no Data sub-struct) so existing accessors are unchanged.
    struct SkyboxComponent
    {
        REFLECT()

        PROPERTY( DisplayName( "Skybox" ), Category( "Skybox" ), Asset<SkyboxAsset> )
        Assets::AssetHandle SkyboxHandle;

        PROPERTY( DisplayName( "Intensity" ), Category( "Skybox" ), Range( 0.0f, 10.0f ) )
        float Intensity = 1.0f;

        // Engine-generated procedural atmosphere (Rayleigh+Mie).
        PROPERTY( DisplayName( "Procedural" ), Category( "Skybox" ) )
        bool  Procedural    = false;
        PROPERTY( DisplayName( "Sun Intensity" ), Category( "Skybox" ), Range( 1.0f, 50.0f ) )
        float SunIntensity  = 22.0f;  // atmosphere sun radiance scale
        PROPERTY( DisplayName( "Sun Disk Size" ), Category( "Skybox" ), Range( 0.002f, 0.1f ) )
        float SunDiskRadius = 0.02f;  // sun angular radius (radians)

        // Engine-generated volumetric clouds (raymarched in the procedural-sky pass; visual only).
        PROPERTY( DisplayName( "Volumetric Clouds" ), Category( "Clouds" ) )
        bool  EnableClouds   = false;
        PROPERTY( DisplayName( "Coverage" ), Category( "Clouds" ), Range( 0.0f, 1.0f ) )
        float CloudCoverage  = 0.5f;   // 0 = clear sky, 1 = overcast
        PROPERTY( DisplayName( "Density" ), Category( "Clouds" ), Range( 0.0f, 3.0f ) )
        float CloudDensity   = 0.6f;   // opacity / extinction multiplier
        PROPERTY( DisplayName( "Cloud Height" ), Category( "Clouds" ), Range( 100.0f, 3000.0f ) )
        float CloudHeight    = 600.0f; // world-space altitude of the cloud layer base
        PROPERTY( DisplayName( "Thickness" ), Category( "Clouds" ), Range( 100.0f, 2000.0f ) )
        float CloudThickness = 500.0f; // vertical extent of the layer
        PROPERTY( DisplayName( "Wind Speed" ), Category( "Clouds" ), Range( 0.0f, 50.0f ) )
        float CloudWindSpeed = 8.0f;   // horizontal drift speed (animation)

        // Transient (not serialized — no PROPERTY): set by the editor's "Bake" button.
        bool  RequestBake   = false;
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
} // namespace Desert::ECS