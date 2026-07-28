#pragma once

#include <Engine/Assets/Shader/ShaderAsset.hpp>
#include <Engine/Graphic/Shader.hpp>

namespace Desert::Runtime
{
    class ShaderService
    {
    public:
        Common::BoolResultStr            Register( const std::shared_ptr<Assets::ShaderAsset>& shaderAsset );
        std::shared_ptr<Graphic::Shader> Get( const Assets::AssetHandle& handle ) const;
        std::shared_ptr<Graphic::Shader> GetByName( const std::string& name ) const;
        void                             Clear();

        // All registered shader program names (for the editor's material shader picker).
        std::vector<std::string> GetAllNames() const;

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Shader>> m_Shaders;
        std::unordered_map<std::string, Assets::AssetHandle>                      m_NameToHandleMap;
        // Named passes of multi-pass shaders, keyed "<Shader>/<Pass>". Kept separate from
        // m_Shaders because several programs share one asset handle.
        std::unordered_map<std::string, std::shared_ptr<Graphic::Shader>> m_PassShaders;
    };
} // namespace Desert::Runtime