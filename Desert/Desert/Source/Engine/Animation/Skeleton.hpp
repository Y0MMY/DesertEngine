#pragma once

#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Animation/AnimationClip.hpp>
#include <Engine/Animation/BoneInfo.hpp>

namespace Desert::Animation
{
    class Skeleton
    {
    public:
        explicit Skeleton( std::vector<BoneInfo>&& bones ) : m_Bones( std::move( bones ) )
        {
            m_Signature = ComputeSignature( m_Bones );
        }

        const std::vector<BoneInfo>& GetBones() const
        {
            return m_Bones;
        }

        uint64_t GetSignature() const
        {
            return m_Signature;
        }

        static uint64_t ComputeSignature( const std::vector<BoneInfo>& bones );

    private:
        std::vector<BoneInfo> m_Bones;
        uint64_t              m_Signature;
    };
} // namespace Desert::Animation
