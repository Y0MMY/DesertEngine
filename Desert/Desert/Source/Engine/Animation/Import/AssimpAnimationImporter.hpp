#pragma once

#include <assimp/scene.h>

#include "Engine/Animation/Skeleton.hpp"

namespace Desert::Animation::Import
{
    class AssimpAnimationImporter
    {
    public:
        static void ImportAnimations( const aiScene* scene, Skeleton& skeleton );
    };
} // namespace Desert::Animation::Import
