#pragma once

#include "MaterialPBRBase.hpp"
#include "PBRPush.hpp"
#include "PBRSceneFrame.hpp"

#include <Engine/Graphic/ShaderProtocols/SkinnedMaterialUB.hpp>

namespace Desert::Graphic
{
    // Skinned PBR material. Like StaticMaterialPBR, its parameters live in the reflected
    // Assets::PBRSurfaceParams and travel via push constants; additionally it uploads the bone matrices
    // (real skinning data) through a storage buffer.
    class SkinnedMaterialPBR final : public MaterialPBRBase
    {
    public:
        struct UpdateSkinnedMaterialPBRInfo
        {
            MaterialInstance* instance = nullptr;

            // The frame's whole SCENE contribution — camera, lights, cascades, environment and the
            // cloud shadow — as MeshRenderer gathered it once for every lit draw in the frame. It is
            // THE snapshot the static path applies, not a copy of the parts of it a skinned mesh was
            // thought to need: this struct used to name the camera, the lights and the cloud shadow
            // one by one, and the two it did not name (the shadow cascades and the environment cubes)
            // silently never reached a skinned mesh at all.
            //
            // A REFERENCE, deliberately. It has no default, so this aggregate cannot be built without
            // one — which is the same defect made impossible instead of merely fixed.
            const PBRSceneFrame& Scene;

            glm::mat4 MeshTransform{ 1.0f };

            ShaderProtocols::SkinnedUB SkinnedUB;
        };

        SkinnedMaterialPBR() : MaterialPBRBase( "SkinnedMaterialPBR", "SkinnedMeshPBR" )
        {
        }

        Assets::PBRSurfaceParams&       Data()       { return m_Data; }
        const Assets::PBRSurfaceParams& Data() const { return m_Data; }

        void Bind( const UpdateSkinnedMaterialPBRInfo& info );

    private:
        // The ONLY thing this material adds to the shared PBR payload: the bone matrices. Everything
        // else a skinned draw needs is scene state and arrives through PBRSceneFrame.
        void UpdateSkinnedUB( const ShaderProtocols::SkinnedUB& skinnedUB );

        Assets::PBRSurfaceParams m_Data;
    };
} // namespace Desert::Graphic
