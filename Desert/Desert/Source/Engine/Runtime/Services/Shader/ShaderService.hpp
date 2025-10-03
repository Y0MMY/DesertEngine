#pragma once

#include <Engine/Assets/Shader/ShaderAsset.hpp>
#include <Engine/Graphic/Shader.hpp>

namespace Desert::Runtime
{
    class ShaderService
    {
    public:
        Common::BoolResultStr               Register( const std::shared_ptr<Assets::ShaderAsset>& shaderAsset );
        std::shared_ptr<Graphic::Shader> Get( const Assets::AssetHandle& handle ) const;
        std::shared_ptr<Graphic::Shader> GetByName( const std::string& name ) const;
        void                             Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Shader>> m_Shaders;
        std::unordered_map<std::string, Assets::AssetHandle>                      m_NameToHandleMap;
    };
} // namespace Desert::Runtime