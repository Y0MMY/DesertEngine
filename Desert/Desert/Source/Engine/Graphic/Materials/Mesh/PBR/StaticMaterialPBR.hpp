#pragma once

#include <Engine/Graphic/Materials/Material.hpp>
#include <Engine/Assets/Mesh/PBRSurfaceParams.hpp>

#include <Engine/Core/Camera.hpp>
#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>
#include <Engine/Graphic/ShaderProtocols/DirectionLight.hpp>

namespace Desert::Graphic
{
    class Image2D;
    class ImageCube;

    // Runtime PBR material. Its parameters live entirely in the reflected PBRSurfaceParams (no per-
    // parameter members or setters): MaterialFactory copies the data from the material asset, the
    // editor edits it via reflection, and the shader receives it automatically (see Bind()).
    class StaticMaterialPBR : public Material
    {
    public:
        StaticMaterialPBR();
        ~StaticMaterialPBR() override = default;

        void Bind( const MaterialInstance* instance ) override;

        Assets::PBRSurfaceParams&       Data()       { return m_Data; }
        const Assets::PBRSurfaceParams& Data() const { return m_Data; }

        // Index into this material's per-object Materials[] storage buffer for the next Bind/draw.
        void SetMaterialIndex( uint32_t index ) { m_MaterialIndex = index; }

        // The per-object transform. It is per-OBJECT, which is why it is still here and why the five
        // per-FRAME appliers that stood beside it are not: those were forwarders to MaterialPBRBase
        // with no caller left once MeshRenderer::FrameState::ApplyTo started writing the snapshot
        // through Engine/Graphic/Materials/SceneLightingBinding.hpp, which reaches a Material directly
        // and therefore reaches the generic mesh path as well.
        static void UpdateTransform( MaterialInstance* instance, const glm::mat4& transform );

    protected:
        // Lets a derived variant bind a different shader (e.g. the instanced StaticMeshPBR_Instanced) while
        // reusing all of StaticMaterialPBR's Update*/Bind/Data plumbing.
        StaticMaterialPBR( std::string&& debugName, std::string&& shaderName );

        void OnBind( MaterialInstance* instance ) override;

    private:
        Assets::PBRSurfaceParams m_Data;
        uint32_t                m_MaterialIndex = 0;
    };

    // Instanced PBR material: identical to StaticMaterialPBR but bound to the StaticMeshPBR_Instanced shader,
    // whose vertex stage reads each instance's model matrix from the InstanceTransforms SSBO (binding 16) by
    // gl_InstanceIndex. Drawn via one instanced RenderMesh call (instanceCount = N) per mesh sub-group.
    class StaticMaterialPBRInstanced : public StaticMaterialPBR
    {
    public:
        StaticMaterialPBRInstanced();
        ~StaticMaterialPBRInstanced() override = default;
    };
} // namespace Desert::Graphic
