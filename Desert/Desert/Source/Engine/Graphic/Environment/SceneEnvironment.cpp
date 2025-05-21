#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{

    Environment EnvironmentManager::Create( const Common::Filepath& filepath )
    {
        const auto& envMapRes = Renderer::GetInstance().CreateEnvironmentMap( filepath );

        return { filepath, envMapRes.EnvironmentMap, envMapRes.IrradianceMap, envMapRes.PrefilteredMap };
    }

} // namespace Desert::Graphic