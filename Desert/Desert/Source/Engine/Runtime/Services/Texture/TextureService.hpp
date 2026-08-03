#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Texture.hpp>

namespace Desert::Runtime
{
    class TextureService
    {
    public:
        // Eager: build the GPU texture now (kept for callers that need it immediately).
        void Register( const std::shared_ptr<Assets::TextureAsset>& texture );

        // Lazy: register the asset SHELL only (no GPU upload). The GPU Texture2D is built on the first Get
        // (the registry path — defers VRAM/upload to first use).
        void RegisterAsset( const std::shared_ptr<Assets::TextureAsset>& texture );

        Graphic::Texture2D*
             Get( const Assets::AssetHandle& handle ) const; // builds-on-miss from a registered shell

        // Original source file of a registered texture (e.g. ".../foo.gif"), empty when the handle is
        // unknown. Loads the shell's .tex metadata on demand — used to detect animated (GIF) sources.
        std::string GetSourcePath( const Assets::AssetHandle& handle ) const;

        void Clear();

    private:
        // Built GPU textures (mutable so the const Get can lazily cache a build).
        mutable std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Texture2D>> m_Textures;
        // Registered shells awaiting a lazy build.
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Assets::TextureAsset>> m_TextureAssets;
    };
} // namespace Desert::Runtime