#pragma once

#include <Engine/Assets/Shader/ShaderAsset.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <unordered_set>

namespace Desert::Runtime
{
    class ShaderService
    {
    public:
        Common::BoolResultStr            Register( const std::shared_ptr<Assets::ShaderAsset>& shaderAsset );
        std::shared_ptr<Graphic::Shader> Get( const Assets::AssetHandle& handle ) const;
        std::shared_ptr<Graphic::Shader> GetByName( const std::string& name ) const;
        void                             Clear();

        /**
         * @brief The program @p name compiled under @p variant — a SECOND object for the same file,
         *        differing only in the bytes substituted for one of its includes.
         *
         * THE CALLER OWNS IT, and that is the design and not an accident. A material authoring a cloud
         * medium produces a new variant on every edit of its graph, and each variant owns VkShaderModules
         * and descriptor set layouts; a service-owned cache would grow by one program per edit for the
         * life of the session and never release one. So the service keeps only a WEAK reference: two
         * layers whose medium is the same text share one program, and the last holder dropping it is what
         * frees the modules. Hand it back by simply releasing the shared_ptr.
         *
         * The default variant is refused rather than served: that is what GetByName is for, and answering
         * it here would build a second, unregistered copy of a program that already exists.
         *
         * @return nullptr when the name is unknown, when @p variant is default, or when the asset behind
         *         the name has expired — each logged with the name, never silently.
         */
        std::shared_ptr<Graphic::Shader> AcquireVariant( const std::string&            name,
                                                         const Graphic::ShaderVariant& variant );

        /**
         * @brief The authored body of a Volume `Medium { ... }` shader, by asset handle — the bytes a
         *        cloud material substitutes into the four programs that sample the cloud field.
         *
         * Read from the asset's CURRENT content on every call rather than cached, deliberately: hot
         * reload re-reads the asset in place, and a cached copy here would be the one thing between an
         * edited graph and a changed picture. It is a few kilobytes of text asked for once per frame at
         * most, against a shader compile if it has changed.
         *
         * @return empty when the handle names nothing or names a shader that is not a medium — logged
         *         once per handle, because "the material points at something that is not a medium" is an
         *         authoring mistake and must not read as "no medium was set". Not const: it latches
         *         that warning, and this is asked every frame.
         */
        std::string MediumSourceOf( const Assets::AssetHandle& handle );

        /// Re-read a medium's body after its file changed on disk. Called by the hot-reload poll, which
        /// is the only thing that knows a shader asset was re-read.
        /// @return true when @p content IS a medium, i.e. when the poll has nothing else to do for it.
        bool RefreshMediumSource( const Assets::AssetHandle& handle, const std::string& content );

        /// Recompile every LIVE variant built from @p handle. The hot-reload poll reloads the program it
        /// registered; without this the variants of that same file would keep the code they were built
        /// with, which reads as "editing the shader stopped working once I authored a medium".
        /// @return how many variants were reloaded.
        int ReloadVariantsOf( const Assets::AssetHandle& handle );

        // All registered shader program names (for the editor's material shader picker).
        //
        // A NAME IN HERE DOES NOT PROMISE THAT GetByName RESOLVES IT. A Volume medium owns its name —
        // it must, or a later shader could claim the same one and the two would fight over it — but it
        // has no Shader object at all, because it is a program fragment. The picker's own loop already
        // skips a name that does not resolve (a shader may also fail to compile), which is what makes
        // this safe; a new caller has to do the same.
        std::vector<std::string> GetAllNames() const;

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Shader>> m_Shaders;
        std::unordered_map<std::string, Assets::AssetHandle>                      m_NameToHandleMap;
        // Named passes of multi-pass shaders, keyed "<Shader>/<Pass>". Kept separate from
        // m_Shaders because several programs share one asset handle.
        std::unordered_map<std::string, std::shared_ptr<Graphic::Shader>> m_PassShaders;

        // The assets the registered programs were built from, so a variant can be compiled from the same
        // source later. WEAK because the asset manager owns them; an expired one is a named refusal.
        std::unordered_map<Assets::AssetHandle, std::weak_ptr<Assets::ShaderAsset>> m_ShaderAssets;

        struct VariantEntry
        {
            Assets::AssetHandle            Handle;
            std::weak_ptr<Graphic::Shader> Program;
        };
        // Keyed "<name>#<16 hex digits of the variant hash>". Weak, for the reason in AcquireVariant.
        std::unordered_map<std::string, VariantEntry> m_Variants;

        // THE BODY OF EVERY REGISTERED Volume MEDIUM, held as TEXT rather than as the asset it came from.
        // A medium's source is needed continuously — the cloud renderer asks for it every frame — and the
        // asset eviction sweep unloads a shader asset nothing holds strongly, which emptied it three
        // seconds into a scene the first time this was written the other way. A few kilobytes per medium
        // is the whole cost, and it makes the answer independent of when the sweep last ran.
        std::unordered_map<Assets::AssetHandle, std::string> m_MediumSources;

        /// Handles already reported as naming something that is not a medium, so the refusal is said once
        /// per handle rather than once per frame.
        std::unordered_set<uint64_t> m_WarnedNotAMedium;
    };
} // namespace Desert::Runtime