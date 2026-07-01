#include "Skeleton.hpp"

#include <algorithm>
#include <string>

namespace Desert::Animation
{
    // ORDER-INDEPENDENT signature: identifies a rig by its SET of (bone name -> parent bone name) edges, NOT by
    // the order bones happen to appear in the array. This matters because the same Mixamo rig exported WITH a
    // skin (character) vs WITHOUT a skin (animation file) yields the bones in different array orders / from
    // different sources (mesh weights vs animation channels) — an order-sensitive hash would give them
    // different signatures and the animation would never match the character. Hashing a sorted set of
    // name<parentName entries makes both produce the SAME signature while still distinguishing different rigs.
    uint64_t Skeleton::ComputeSignature( const std::vector<BoneInfo>& bones )
    {
        std::vector<std::string> entries;
        entries.reserve( bones.size() );
        for ( const auto& bone : bones )
        {
            const std::string parentName =
                 ( bone.ParentBoneID.has_value() && bone.ParentBoneID.value() < bones.size() )
                      ? bones[bone.ParentBoneID.value()].Name
                      : std::string();
            entries.push_back( bone.Name + '<' + parentName );
        }
        std::sort( entries.begin(), entries.end() );

        uint64_t hash = 1469598103934665603ULL;
        for ( const auto& entry : entries )
            for ( char c : entry )
                hash = ( hash ^ static_cast<unsigned char>( c ) ) * 1099511628211ULL;

        return hash;
    }
} // namespace Desert::Animation
