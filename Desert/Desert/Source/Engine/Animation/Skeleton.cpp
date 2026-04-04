#include "Skeleton.hpp"

namespace Desert::Animation
{
    uint64_t Skeleton::ComputeSignature( const std::vector<BoneInfo>& bones )
    {
        uint64_t hash = 1469598103934665603ULL;

        for ( const auto& bone : bones )
        {
            for ( char c : bone.Name )
            {
                hash = ( hash ^ c ) * 1099511628211ULL;
            }

            uint64_t parent = bone.ParentBoneID.value_or( 0xffffffff );
            hash ^= parent + 0x9e3779b97f4a7c15ULL + ( hash << 6 ) + ( hash >> 2 );
        }

        return hash;
    }
} // namespace Desert::Animation
