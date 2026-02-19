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
        }

        const std::vector<BoneInfo>& GetBones() const
        {
            return m_Bones;
        }

        void AddClip( const std::string& name, std::shared_ptr<AnimationClip> clip )
        {
            m_Clips.emplace( name, std::move( clip ) );
        }

        std::shared_ptr<AnimationClip> GetClip( const std::string& name ) const
        {
            auto it = m_Clips.find( name );
            return it != m_Clips.end() ? it->second : nullptr;
        }

        const auto& GetClips() const
        {
            return m_Clips;
        }

    private:
        std::vector<BoneInfo>                                           m_Bones;
        std::unordered_map<std::string, std::shared_ptr<AnimationClip>> m_Clips;
    };
} // namespace Desert::Animation
