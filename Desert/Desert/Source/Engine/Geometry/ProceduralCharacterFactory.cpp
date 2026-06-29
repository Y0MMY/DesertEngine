#include "ProceduralCharacterFactory.hpp"

#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Animation/Skeleton.hpp>
#include <Engine/Animation/BoneInfo.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace Desert::Geometry
{
    namespace
    {
        // ---- Skeleton joints (index order is the bone array order; parents reference these indices) ----
        enum Joint : uint32_t
        {
            Hips = 0,
            Spine,
            Chest,
            Neck,
            Head,
            ShoulderL,
            ElbowL,
            HandL,
            ShoulderR,
            ElbowR,
            HandR,
            HipL,
            KneeL,
            FootL,
            HipR,
            KneeR,
            FootR,
            JointCount
        };

        struct JointDef
        {
            const char* Name;
            int         Parent;     // -1 = root
            glm::vec3   BindWorld;  // T/A-pose position in mesh space (origin at feet, +Y up, meters)
        };

        // A-pose: arms hang straight down so EVERY segment box is axis-aligned (no orientation maths needed).
        constexpr JointDef kJoints[JointCount] = {
            { "Hips", -1, { 0.00f, 0.95f, 0.0f } },     { "Spine", Hips, { 0.00f, 1.15f, 0.0f } },
            { "Chest", Spine, { 0.00f, 1.40f, 0.0f } }, { "Neck", Chest, { 0.00f, 1.55f, 0.0f } },
            { "Head", Neck, { 0.00f, 1.65f, 0.0f } },

            { "Shoulder.L", Chest, { 0.18f, 1.45f, 0.0f } },
            { "Elbow.L", ShoulderL, { 0.18f, 1.15f, 0.0f } },
            { "Hand.L", ElbowL, { 0.18f, 0.88f, 0.0f } },

            { "Shoulder.R", Chest, { -0.18f, 1.45f, 0.0f } },
            { "Elbow.R", ShoulderR, { -0.18f, 1.15f, 0.0f } },
            { "Hand.R", ElbowR, { -0.18f, 0.88f, 0.0f } },

            { "Hip.L", Hips, { 0.09f, 0.92f, 0.0f } },
            { "Knee.L", HipL, { 0.09f, 0.50f, 0.0f } },
            { "Foot.L", KneeL, { 0.09f, 0.06f, 0.0f } },

            { "Hip.R", Hips, { -0.09f, 0.92f, 0.0f } },
            { "Knee.R", HipR, { -0.09f, 0.50f, 0.0f } },
            { "Foot.R", KneeR, { -0.09f, 0.06f, 0.0f } },
        };

        // ---- A body-segment box, rigid-skinned 100% to one joint ----
        struct BoxDef
        {
            glm::vec3 Center;
            glm::vec3 HalfExtents;
            uint32_t  BoneID;
        };

        const std::array<BoxDef, 14> kBoxes = { {
            { { 0.00f, 1.00f, 0.0f }, { 0.16f, 0.12f, 0.11f }, Hips },   // pelvis
            { { 0.00f, 1.33f, 0.0f }, { 0.18f, 0.20f, 0.12f }, Chest },  // torso
            { { 0.00f, 1.58f, 0.0f }, { 0.05f, 0.06f, 0.05f }, Neck },   // neck
            { { 0.00f, 1.75f, 0.0f }, { 0.11f, 0.12f, 0.11f }, Head },   // head

            { { 0.18f, 1.30f, 0.0f }, { 0.06f, 0.16f, 0.06f }, ShoulderL }, // upper arm L
            { { 0.18f, 1.015f, 0.0f }, { 0.05f, 0.15f, 0.05f }, ElbowL },   // forearm L
            { { -0.18f, 1.30f, 0.0f }, { 0.06f, 0.16f, 0.06f }, ShoulderR }, // upper arm R
            { { -0.18f, 1.015f, 0.0f }, { 0.05f, 0.15f, 0.05f }, ElbowR },   // forearm R

            { { 0.09f, 0.71f, 0.0f }, { 0.075f, 0.22f, 0.075f }, HipL },  // thigh L
            { { 0.09f, 0.28f, 0.0f }, { 0.06f, 0.22f, 0.06f }, KneeL },   // shin L
            { { 0.09f, 0.04f, 0.08f }, { 0.06f, 0.04f, 0.12f }, FootL },  // foot L
            { { -0.09f, 0.71f, 0.0f }, { 0.075f, 0.22f, 0.075f }, HipR }, // thigh R
            { { -0.09f, 0.28f, 0.0f }, { 0.06f, 0.22f, 0.06f }, KneeR },  // shin R
            { { -0.09f, 0.04f, 0.08f }, { 0.06f, 0.04f, 0.12f }, FootR }, // foot R
        } };

        // Cube faces: outward normal + the 4 corner sign-triples in CCW order (verified by right-hand rule).
        struct Face
        {
            glm::vec3                      Normal;
            std::array<glm::vec3, 4>       Signs; // each component is -1 or +1
        };

        const std::array<Face, 6> kFaces = { {
            { { 1, 0, 0 }, { { { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } } } },     // +X
            { { -1, 0, 0 }, { { { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } } } }, // -X
            { { 0, 1, 0 }, { { { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } } } },      // +Y
            { { 0, -1, 0 }, { { { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } } } }, // -Y
            { { 0, 0, 1 }, { { { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } } } },      // +Z
            { { 0, 0, -1 }, { { { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } } } }, // -Z
        } };

        void AppendBox( std::vector<SkinnedVertex>& verts, std::vector<Index>& indices, const BoxDef& box )
        {
            constexpr glm::vec2 kUV[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

            for ( const Face& face : kFaces )
            {
                const uint32_t base = static_cast<uint32_t>( verts.size() );

                glm::vec3 corner0 = box.Center + face.Signs[0] * box.HalfExtents;
                glm::vec3 corner1 = box.Center + face.Signs[1] * box.HalfExtents;
                const glm::vec3 tangent = glm::normalize( corner1 - corner0 );
                const glm::vec3 bitan   = glm::cross( face.Normal, tangent );

                for ( int i = 0; i < 4; ++i )
                {
                    SkinnedVertex v{};
                    v.StaticVertex.Position  = box.Center + face.Signs[i] * box.HalfExtents;
                    v.StaticVertex.Normal    = face.Normal;
                    v.StaticVertex.Tangent   = tangent;
                    v.StaticVertex.Bitangent = bitan;
                    v.StaticVertex.TexCoord  = kUV[i];
                    v.BoneIDs[0]             = box.BoneID;
                    v.BoneWeights[0]         = 1.0f;
                    verts.push_back( v );
                }

                indices.push_back( { base + 0, base + 1, base + 2 } );
                indices.push_back( { base + 0, base + 2, base + 3 } );
            }
        }

        std::vector<Animation::BoneInfo> BuildBones()
        {
            std::vector<Animation::BoneInfo> bones( JointCount );
            for ( uint32_t i = 0; i < JointCount; ++i )
            {
                const JointDef& j = kJoints[i];
                glm::vec3 parentWorld = ( j.Parent >= 0 ) ? kJoints[j.Parent].BindWorld : glm::vec3( 0.0f );

                bones[i].BoneIndex = i;
                bones[i].Name      = j.Name;
                // No rotation in the bind pose, so the local transform is just the offset from the parent.
                bones[i].LocalBindTransform = glm::translate( glm::mat4( 1.0f ), j.BindWorld - parentWorld );
                bones[i].OffsetMatrix       = glm::mat4( 1.0f ); // recomputed below
                if ( j.Parent >= 0 )
                    bones[i].ParentBoneID = static_cast<uint32_t>( j.Parent );
            }
            return bones;
        }

        // Owns the humanoid skeleton for the process lifetime (SkinnedMesh only holds a const Skeleton*).
        std::unique_ptr<Animation::Skeleton> s_Skeleton;
        std::optional<Assets::AssetHandle>   s_Handle;
        uint64_t                             s_Signature = 0;

        void BuildOnce()
        {
            if ( s_Handle.has_value() )
                return;

            s_Skeleton = std::make_unique<Animation::Skeleton>( BuildBones() );
            s_Skeleton->RecomputeOffsetMatrices(); // signature is name/parent based, so this stays valid
            s_Signature = s_Skeleton->GetSignature();

            std::vector<SkinnedVertex> verts;
            std::vector<Index>         indices;
            for ( const BoxDef& box : kBoxes )
                AppendBox( verts, indices, box );

            // Single submesh covering the whole body.
            Submesh sub{};
            sub.Name        = "Humanoid";
            sub.VertexOffset = 0;
            sub.VertexCount  = static_cast<uint32_t>( verts.size() );
            sub.IndexOffset  = 0;
            sub.IndexCount   = static_cast<uint32_t>( indices.size() * 3 );
            sub.Transform    = glm::mat4( 1.0f );
            sub.BoundingBox  = { glm::vec3( -0.30f, 0.0f, -0.20f ), glm::vec3( 0.30f, 1.90f, 0.20f ) };

            auto mesh = std::make_shared<SkinnedMesh>( verts, indices, std::vector<Submesh>{ sub },
                                                       s_Skeleton.get() );
            s_Handle = Runtime::ResourceRegistry::GetMeshService()->RegisterProcedural( mesh );
        }
    } // namespace

    Assets::AssetHandle ProceduralCharacterFactory::GetHumanoidMesh()
    {
        BuildOnce();
        return *s_Handle;
    }

    uint64_t ProceduralCharacterFactory::GetHumanoidSkeletonSignature()
    {
        BuildOnce();
        return s_Signature;
    }

    const Animation::Skeleton* ProceduralCharacterFactory::GetHumanoidSkeleton()
    {
        BuildOnce();
        return s_Skeleton.get();
    }
} // namespace Desert::Geometry
