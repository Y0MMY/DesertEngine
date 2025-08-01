#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{

    Environment EnvironmentManager::Create( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset )
    {
        const auto& envMapRes =
             Renderer::GetInstance().CreateEnvironmentMap( skyboxAsset->GetMetadata().Filepath ); // TODO: Raw data

        return { skyboxAsset->GetMetadata().Filepath, envMapRes.EnvironmentMap, envMapRes.IrradianceMap,
                 envMapRes.PrefilteredMap };
    }

} // namespace Desert::Graphic