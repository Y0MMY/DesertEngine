#include "ShaderService.hpp"

#include <Common/Core/Logger.hpp>

#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

#include <format>

namespace Desert::Runtime
{

    Common::BoolResultStr ShaderService::Register( const std::shared_ptr<Assets::ShaderAsset>& shaderAsset )
    {
        if ( !shaderAsset->GetMetadata().IsValid() )
        {
            return Common::MakeError( "Shader asset is invalid" );
        }

        // A MEDIUM PROGRAM IS SOURCE, NOT A PROGRAM. It declares no stages because it is compiled INTO
        // four other programs as the substitution for one of their includes; building a Shader object for
        // it would produce one with no modules, and the honest complaint below ("registered but has no
        // compiled stages — every material using it will not draw") would be a lie about a file that is
        // working exactly as intended. It is still recorded by name and by handle, because that is how a
        // material points at it and how the renderer fetches its text.
        const auto meta = Core::Preprocess::ShaderPreprocess::ParseProgramMeta( shaderAsset->GetShaderContent() );
        if ( meta.IsMediumProgram() )
        {
            const auto name         = shaderAsset->GetMetadata().Filepath.stem().string();
            m_NameToHandleMap[name] = shaderAsset->GetMetadata().Handle;
            m_ShaderAssets[shaderAsset->GetMetadata().Handle] = shaderAsset;
            LOG_INFO( "[ShaderService] '{}' is a Volume medium ({} bytes of authored source); it compiles "
                      "into the programs that sample the cloud field rather than into one of its own.",
                      name, meta.MediumSource.size() );
            return BOOLSUCCESS;
        }

        const auto shader                            = Graphic::Shader::Create( shaderAsset );
        m_Shaders[shaderAsset->GetMetadata().Handle] = shader;
        m_NameToHandleMap[shader->GetName()]         = shaderAsset->GetMetadata().Handle;
        // Kept so AcquireVariant can compile the SAME source under a substitution later. Weak: the
        // asset manager owns the asset, and this service must not extend its life.
        m_ShaderAssets[shaderAsset->GetMetadata().Handle] = shaderAsset;
        // Whose the shader is, in the ledger — see Engine/Graphic/ResourceLedger.hpp.
        shader->ClaimOwnership( Graphic::ResourceOwner::AssetService, shaderAsset->GetMetadata().Handle );

        // Registered either way, deliberately: a shader that fails to compile must keep its NAME, or the
        // material referencing it silently falls back to the standard one and the artist is told nothing.
        // It is registered and unusable, and it says so once, here, naming itself — the per-stage error
        // above names a file and a line, which is not the same as naming the shader a material asks for.
        if ( !shader->IsCompiled() )
            LOG_ERROR( "[ShaderService] '{}' registered but has no compiled stages — every material using "
                       "it will not draw until it compiles ({}).",
                       shader->GetName(), shaderAsset->GetMetadata().Filepath.string() );

        // DSL multi-pass shaders: every named pass is its own program, addressable as
        // "<Shader>/<Pass>" (e.g. GetByName("Unlit/Shadow")).
        for ( const auto& passName : shader->GetProgramMeta().PassNames )
        {
            auto passShader                      = Graphic::Shader::Create( shaderAsset, {}, passName );
            m_PassShaders[passShader->GetName()] = passShader;
            passShader->ClaimOwnership( Graphic::ResourceOwner::AssetService, shaderAsset->GetMetadata().Handle );
        }

        return BOOLSUCCESS;
    }

    std::shared_ptr<Graphic::Shader> ShaderService::GetByName( const std::string& name ) const
    {
        auto handleIt = m_NameToHandleMap.find( name );
        if ( handleIt != m_NameToHandleMap.end() )
        {
            return Get( handleIt->second );
        }

        auto passIt = m_PassShaders.find( name );
        if ( passIt != m_PassShaders.end() )
        {
            return passIt->second;
        }
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::Shader> ShaderService::Get( const Assets::AssetHandle& handle ) const
    {
        auto it = m_Shaders.find( handle );
        return ( it != m_Shaders.end() ) ? it->second : nullptr;
    }

    std::shared_ptr<Graphic::Shader> ShaderService::AcquireVariant( const std::string&            name,
                                                                    const Graphic::ShaderVariant& variant )
    {
        // The default variant is GetByName's question, and answering it here would build an unregistered
        // second copy of a program that already exists — two objects, two sets of modules, and a
        // material picking whichever it was handed.
        if ( variant.IsDefault() )
        {
            LOG_ERROR( "[ShaderService] AcquireVariant('{}') was asked for the DEFAULT variant. That is "
                       "GetByName's question; serving it here would build a second copy of a registered "
                       "program.",
                       name );
            return nullptr;
        }

        const auto handleIt = m_NameToHandleMap.find( name );
        if ( handleIt == m_NameToHandleMap.end() )
        {
            LOG_ERROR( "[ShaderService] AcquireVariant: no program named '{}' is registered.", name );
            return nullptr;
        }

        const std::string key =
             std::format( "{}#{:016x}", name, static_cast<unsigned long long>( variant.Hash() ) );

        if ( const auto it = m_Variants.find( key ); it != m_Variants.end() )
        {
            if ( auto live = it->second.Program.lock() )
                return live;
            m_Variants.erase( it ); // the last holder let it go; build a fresh one below
        }

        const auto assetIt = m_ShaderAssets.find( handleIt->second );
        auto       asset   = assetIt != m_ShaderAssets.end() ? assetIt->second.lock() : nullptr;
        if ( !asset )
        {
            LOG_ERROR( "[ShaderService] AcquireVariant('{}'): the shader asset behind that name is gone.", name );
            return nullptr;
        }

        auto program = Graphic::Shader::Create( asset, variant );
        program->ClaimOwnership( Graphic::ResourceOwner::AssetService, handleIt->second );
        if ( !program->IsCompiled() )
        {
            // Named out loud and still handed back: the caller decides whether to draw with the default
            // instead, and a nullptr here would be indistinguishable from "the name is unknown".
            LOG_ERROR( "[ShaderService] Variant {} of '{}' has no compiled stages — the substituted "
                       "source did not compile.",
                       key, name );
        }

        m_Variants[key] = VariantEntry{ handleIt->second, program };
        return program;
    }

    std::string ShaderService::MediumSourceOf( const Assets::AssetHandle& handle ) const
    {
        if ( handle.IsNull() )
            return {};

        const auto it    = m_ShaderAssets.find( handle );
        auto       asset = it != m_ShaderAssets.end() ? it->second.lock() : nullptr;
        if ( !asset )
        {
            LOG_ERROR( "[ShaderService] a cloud material names medium shader {} and no such shader asset "
                       "is registered — the layer will draw the DEFAULT medium.",
                       static_cast<uint64_t>( handle ) );
            return {};
        }

        auto meta = Core::Preprocess::ShaderPreprocess::ParseProgramMeta( asset->GetShaderContent() );
        if ( !meta.IsMediumProgram() )
        {
            // Pointing a Medium slot at an ordinary shader is an authoring mistake with a picture that
            // looks exactly like "I forgot to set it". Named, so it does not.
            LOG_ERROR( "[ShaderService] '{}' is in a cloud material's Medium slot but declares no Medium "
                       "block, so it cannot be an authored medium. The layer will draw the DEFAULT one.",
                       asset->GetMetadata().Filepath.string() );
            return {};
        }
        return std::move( meta.MediumSource );
    }

    int ShaderService::ReloadVariantsOf( const Assets::AssetHandle& handle )
    {
        int reloaded = 0;
        for ( auto it = m_Variants.begin(); it != m_Variants.end(); )
        {
            auto program = it->second.Handle == handle ? it->second.Program.lock() : nullptr;
            if ( it->second.Program.expired() )
            {
                it = m_Variants.erase( it );
                continue;
            }
            if ( program )
            {
                const auto res = program->Reload();
                if ( !res )
                {
                    LOG_ERROR( "[ShaderService] Variant {} failed to recompile: {}", it->first, res.GetError() );
                }
                else
                {
                    ++reloaded;
                }
            }
            ++it;
        }
        return reloaded;
    }

    void ShaderService::Clear()
    {
        // Was an empty body. Shaders own VkShaderModules and the pipeline layouts built from them.
        m_Shaders.clear();
        m_PassShaders.clear();
        m_NameToHandleMap.clear();
        m_ShaderAssets.clear();
        // Only the weak bookkeeping — a variant's modules belong to whoever still holds it, and freeing
        // them from here would leave that holder with a program made of destroyed modules.
        m_Variants.clear();
    }

    std::vector<std::string> ShaderService::GetAllNames() const
    {
        std::vector<std::string> names;
        names.reserve( m_NameToHandleMap.size() );
        for ( const auto& [name, handle] : m_NameToHandleMap )
            names.push_back( name );
        return names;
    }

} // namespace Desert::Runtime