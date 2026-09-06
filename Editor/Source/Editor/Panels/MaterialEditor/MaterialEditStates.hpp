#pragma once

#include <Editor/Core/EditableProperty.hpp>

#include <Engine/Assets/MaterialData.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>

#include <string>
#include <vector>

namespace Desert::Editor::MaterialEdit
{
    // THE AUTHORED HALF OF A MATERIAL — the shader it draws with and the values it draws with. Everything
    // the Material Editor can change, and nothing else.
    //
    // The other half is IDENTITY: MaterialId, which is this asset's own name in the asset database, and
    // ParentMaterialId, which is its place in the material-instance chain. Keeping the two halves apart is
    // load-bearing rather than tidy, because a document's WORKING COPY is a second material asset
    // registered beside the subject (see SurfaceMaterialAsset::CreateWorkingCopy for why it has to be a
    // second asset and not a spare struct). The copy therefore carries a MaterialId of its own, and a
    // whole-struct assignment in either direction would hand the subject's id to the copy on Discard, or
    // the copy's id to the subject on Apply. Both are the same failure: MaterialService keys the
    // mesh -> material link on exactly that id, so whichever of the two registered first would then answer
    // for both, and a mesh in the level would start drawing the preview's material.
    //
    // ParentMaterialId is copied by NEITHER function, for the same reason and one more: it names somebody
    // else. An instance's working copy must resolve through the same parent chain the subject does, and
    // this window offers no way to re-parent a material, so a transfer of it could only ever be an
    // accident.

    // Do these two materials draw the same picture? Order-insensitive by name, because MaterialData stores
    // its parameters in a vector that grows in the order they were first written — two materials with the
    // same values authored in a different order are the same material, and comparing the vectors
    // positionally would report a document as permanently unapplied after a Discard.
    //
    // The shader is compared through EffectiveShaderName() rather than the optional, so an absent name and
    // an explicit "StaticMeshPBR" compare EQUAL. They are the same shader; a material that was saved
    // before the field existed must not read as differing from the one the editor just wrote.
    [[nodiscard]] inline bool AuthoredValuesEqual( const Assets::MaterialData& a, const Assets::MaterialData& b )
    {
        if ( a.EffectiveShaderName() != b.EffectiveShaderName() )
            return false;

        if ( a.Params.size() != b.Params.size() || a.Textures.size() != b.Textures.size() )
            return false;

        // Sizes agree and MaterialData::SetParam/SetTexture never store a name twice, so "every entry of a
        // has an equal partner in b" is a full comparison rather than a one-way containment test.
        for ( const auto& param : a.Params )
        {
            const glm::vec4* other = b.FindParam( param.Name );
            if ( !other || *other != param.Value )
                return false;
        }

        for ( const auto& texture : a.Textures )
        {
            // GetTexture answers 0 both for "not bound" and for "absent", which is right here: a slot
            // explicitly bound to nothing and a slot never written are the same material.
            if ( b.GetTexture( texture.Name ) != texture.TextureHandle )
                return false;
        }

        return true;
    }

    // Move the authored half of @p source into @p destination, leaving @p destination's identity alone.
    // The single transfer both Apply (working -> applied) and Discard (applied -> working) are written in,
    // so the two directions cannot drift into carrying different fields.
    inline void CopyAuthoredValues( Assets::MaterialData& destination, const Assets::MaterialData& source )
    {
        destination.ShaderName = source.ShaderName;
        destination.Params     = source.Params;
        destination.Textures   = source.Textures;
    }

    // THE TWO "DIRTY"S, AND THEY ARE NOT ONE FLAG.
    //
    // `Unapplied` is about the SCENE: the artist has moved something that every mesh in every open scene is
    // still not showing. It is what decides whether Apply and Discard are offered.
    //
    // `Unsaved` is about the FILE: it is the document's dirty mark, the dot on the tab, the question on
    // close. It is measured against the WORKING state and not against the applied one, because an edit
    // that has not even been applied is still an edit the file does not have — a document that reported
    // itself clean between an edit and an Apply would lose that edit to a close with no question asked.
    //
    // Both are DERIVED from the three states rather than remembered as booleans set at the edit sites.
    // Flags of this kind are the shape of defect this engine keeps paying for: there is always one site
    // that forgets to set one (the Material Editor's own re-push flag was rewritten into a derived
    // identity for exactly this reason, see MaterialEditorPanel::PushedIdentity). Derived costs a walk of
    // a handful of named values once per frame and cannot be forgotten.
    struct DirtyState
    {
        bool Unapplied = false; // working != applied: the scene is not showing these edits
        bool Unsaved   = false; // working != on disk: the file does not have them
    };

    [[nodiscard]] inline DirtyState EvaluateDirty( const Assets::MaterialData& working,
                                                   const Assets::MaterialData& applied,
                                                   const Assets::MaterialData& onDisk )
    {
        DirtyState state;
        state.Unapplied = !AuthoredValuesEqual( working, applied );
        state.Unsaved   = !AuthoredValuesEqual( working, onDisk );
        return state;
    }

    /**
     * @brief DOES PUBLISHING THIS MATERIAL OWE THE SCENE ITS GLOBAL STAMP?
     *
     * MaterialService::BumpInvalidationVersion moves ONE counter that every mesh component in every open
     * scene compares itself against (Components.hpp SeenMaterialsVersion, MeshECSSystem.hpp:129). Each one
     * that differs throws away its cached RuntimeMaterialInstances and builds one MaterialInstance per slot
     * again on the next tick. That is the right price for an edit the scene must see, and the wrong price
     * for one it must not — and until this rule existed, EVERY FRAME of a drag paid it for values that by
     * construction stop at this window's preview. Measured on a 30-step drag: 30 stamps, now 0.
     *
     * IT BECAME A COST THE MOMENT STAGING LANDED, and not before. Until then a publish was only ever called
     * with the subject, so "the material was re-valued" and "the scene changed" were the same event. They
     * are two events now, and only one of them owes the stamp.
     *
     * WHO CACHES THE VALUES DECIDES IT, not which asset it is:
     *
     *   A BASE material's values live in the runtime Material the publish re-values in place. Anything
     *   rendering that asset directly — including the preview's own entity — reads through it and needs
     *   nothing. What caches is a CHILD INSTANCE, which bakes its overrides in at creation. A working copy
     *   is created with a freshly generated MaterialId that no file on disk names as a parent, so it HAS no
     *   children and the stamp would reach nobody who could care.
     *
     *   An INSTANCE has no runtime Material of its own; its overrides live in the cached MaterialInstance
     *   CreateRuntimeInstance built, and dropping that cache is the only lever there is. The preview's own
     *   entity holds one, so an instance's working copy still pays. Narrowing THAT needs a way to
     *   invalidate one scene's cached instances, which does not exist and which the Material Editor should
     *   not grow on its own — it is the renderer's vocabulary, not a document's.
     *
     * A RULE AND NOT AN `if` AT THE CALL SITE. MaterialEditorPanel.cpp is compiled by no suite at all
     * (scripts/CI/UnreachedSources.sh), so a decision written there is a decision nothing can show going
     * red — and this one is invisible when it is wrong: the pictures are identical either way, and only a
     * counter tells them apart.
     *
     * @param isInstance is the published material an instance of another?
     * @param isSubject  is it the document's SUBJECT — the asset the scenes render — rather than its
     *                   working copy?
     */
    [[nodiscard]] inline constexpr bool PublishOwesTheGlobalStamp( bool isInstance, bool isSubject ) noexcept
    {
        return isInstance || isSubject;
    }

    // ── THE PROPERTY CENSUS, DERIVED FROM THE SHADER'S SCHEMA ──────────────────────────────────────────
    //
    // WHAT THE CONTROL CHANNEL MAY WRITE IS NOT A LIST ANYBODY MAINTAINS. It is the shader's own
    // `Properties` block, read through ShaderProgramMeta — the SAME declaration MaterialEditorPanel::
    // DrawParameters walks to decide which rows exist and what widget each one gets.
    //
    // The alternative was a table of names in the channel's own code. It would have been a THIRD list of
    // parameter names beside the schema and the table, and this project closed two "a middle link drops a
    // property" defects on the day this was written. The third list does not exist here: there is one
    // walk of `schema.Params`, and everything below reads it.
    //
    // PURE, and that is load-bearing. MaterialEditorPanel.cpp cannot be reached by any test suite (it owns
    // a PreviewViewport, which owns a SceneRenderer, which needs a Vulkan device), so a census written
    // there would be a rule nothing could ever show going red. Here it is a function over two structs.

    /// How many components of a vec4 a schema type actually uses. ONE rule, asked by the census, by the
    /// refusal that counts a caller's numbers, and by the widget. A `float` given three numbers is a
    /// caller who meant a different parameter, and finding out from the picture is finding out too late.
    [[nodiscard]] inline int ComponentsOf( ::Desert::Core::Formats::ShaderValueType type )
    {
        using VT = ::Desert::Core::Formats::ShaderValueType;
        switch ( type )
        {
            case VT::Float2:
            case VT::Int2:
                return 2;
            case VT::Float3:
            case VT::Int3:
                return 3;
            case VT::Float4:
            case VT::Int4:
                return 4;
            default:
                // Float, Int, UInt, Bool and Unknown all live in x. Not a fallback standing in for a
                // missing case: a scalar IS one component, and Unknown is a schema the parser could not
                // type, which must not be guessed wider than the narrowest thing it could be.
                return 1;
        }
    }

    /// What a property is CALLED on the wire. The schema's type, refined by the widget where the widget
    /// changes what a caller must send: a Color is still four floats but a client that sees "color" knows
    /// they are not metres.
    [[nodiscard]] inline std::string TypeNameOf( const ::Desert::Core::Formats::ShaderParam& p )
    {
        using VT = ::Desert::Core::Formats::ShaderValueType;
        using W  = ::Desert::Core::Formats::ShaderParamWidget;

        if ( p.IsAssetRef() )
            return p.AssetKind;
        if ( p.IsTexture )
            return p.IsCubeTexture ? "textureCube" : "texture";
        if ( p.Widget == W::Color )
            return "color";

        switch ( p.Type )
        {
            case VT::Float:
                return "float";
            case VT::Float2:
                return "float2";
            case VT::Float3:
                return "float3";
            case VT::Float4:
                return "float4";
            case VT::Int:
                return "int";
            case VT::Int2:
                return "int2";
            case VT::Int3:
                return "int3";
            case VT::Int4:
                return "int4";
            case VT::UInt:
                return "uint";
            case VT::Bool:
                return "bool";
            default:
                return "unknown";
        }
    }

    /// The value a row SHOWS: this material's own override, else the parent's effective value (instance
    /// mode), else the schema default.
    ///
    /// ONE SEEDING RULE, called by the widget and by the census. Two copies of it would eventually differ
    /// by one fallback, and the symptom would be a client reading a number the window is not displaying —
    /// which is worse than no number, because a report quotes it beside a picture.
    [[nodiscard]] inline glm::vec4 EffectiveParamValue( const Assets::MaterialData&                 data,
                                                        const Assets::MaterialData*                 parentData,
                                                        const ::Desert::Core::Formats::ShaderParam& p )
    {
        const glm::vec4 fallback = parentData ? parentData->GetParam( p.Name, p.Default ) : p.Default;
        return data.GetParam( p.Name, fallback );
    }

    /// Why the channel cannot WRITE this property, or empty when it can.
    ///
    /// Asked by the census (to fill EditableProperty::NotSettableReason) and by `set` itself (to refuse),
    /// so the list a client reads and the answer it gets cannot disagree about one row. Splitting them was
    /// tried in every subsystem this project has and the two halves always drift.
    [[nodiscard]] inline std::string UnsettableReason( const ::Desert::Core::Formats::ShaderParam& p,
                                                       bool                                        isInstance )
    {
        // The window itself draws "from parent material" over both kinds of row in instance mode — the
        // reason is read off the same fact rather than invented here, so the two cannot disagree about
        // which rows an instance may write.
        const char* const inheritedByAnInstance =
             isInstance ? " An instance takes them whole from its parent in any case: per-instance "
                          "descriptors are a v2."
                        : "";

        if ( p.IsAssetRef() )
        {
            return "'" + p.Name + "' is a reference to a " + p.AssetKind +
                   ", not a value. It is bound by picking or dropping an asset, and an asset is not "
                   "something this request can carry." +
                   inheritedByAnInstance;
        }
        if ( p.IsTexture )
        {
            return "'" + p.Name +
                   "' is a texture slot. It is bound by dropping an asset on it, and an asset is not "
                   "something this request can carry." +
                   inheritedByAnInstance;
        }

        // Everything else is a uniform-buffer field, and an INSTANCE's value params are precisely what it
        // is allowed to override — so instance mode adds no refusal here.
        return {};
    }

    /// @p name in @p schema, or null with @p outRefusal saying why — never null and silent.
    ///
    /// A NAME THE SCHEMA DOES NOT DECLARE IS AN ERROR, not a value written and never read. Written, it
    /// would be serialised into the material, ignored by every shader, and the capture taken to prove the
    /// edit would render as "nothing moved" — indistinguishable from the feature being broken.
    [[nodiscard]] inline const ::Desert::Core::Formats::ShaderParam*
    FindSettableParam( const ::Desert::Core::Formats::ShaderProgramMeta& schema, const std::string& name,
                       bool isInstance, std::string& outRefusal )
    {
        const ::Desert::Core::Formats::ShaderParam* found = nullptr;
        for ( const auto& p : schema.Params )
        {
            if ( p.Name == name )
            {
                found = &p;
                break;
            }
        }

        if ( !found )
        {
            std::string known;
            for ( const auto& p : schema.Params )
            {
                if ( p.IsTexture || p.IsAssetRef() )
                    continue;
                if ( !known.empty() )
                    known += ", ";
                known += p.Name;
            }
            // The offer is built from the schema that was just walked, so a shader whose properties
            // changed cannot leave a stale suggestion behind.
            outRefusal = "'" + name + "' is not a property this document's shader declares. It offers: " +
                         ( known.empty() ? std::string( "nothing that carries a value." ) : known );
            return nullptr;
        }

        outRefusal = UnsettableReason( *found, isInstance );
        return outRefusal.empty() ? found : nullptr;
    }

    /// EVERY property the document offers, in the order the shader declares them — which is the order the
    /// window draws them, so a client reading this list and a person reading the panel walk the same rows.
    ///
    /// @p parentData is the parent material's data in instance mode, null for a base material.
    [[nodiscard]] inline std::vector<EditableProperty>
    DescribeProperties( const ::Desert::Core::Formats::ShaderProgramMeta& schema, const Assets::MaterialData& data,
                        const Assets::MaterialData* parentData, bool isInstance )
    {
        std::vector<EditableProperty> properties;
        properties.reserve( schema.Params.size() );

        for ( const auto& p : schema.Params )
        {
            EditableProperty entry;
            entry.Name       = p.Name;
            entry.Label      = p.DisplayName.empty() ? p.Name : p.DisplayName;
            entry.Type       = TypeNameOf( p );
            entry.Components = ComponentsOf( p.Type );
            entry.Min        = p.Min;
            entry.Max        = p.Max;

            // A row with its own entry in the child IS an override — the same test the window's star
            // draws from, so the census and the panel mark the same rows.
            entry.OverridesParent = isInstance && !p.IsTexture && data.FindParam( p.Name ) != nullptr;

            entry.NotSettableReason = UnsettableReason( p, isInstance );
            entry.Settable          = entry.NotSettableReason.empty();

            if ( !p.IsTexture && !p.IsAssetRef() )
            {
                const glm::vec4 value = EffectiveParamValue( data, parentData, p );
                for ( int i = 0; i < entry.Components; ++i )
                    entry.Value[static_cast<std::size_t>( i )] = value[i];
            }
            else
            {
                // A texture's value is an asset handle, and the array carries floats. Reported as zero
                // components rather than as a float that happens to hold a 64-bit id badly: a number a
                // client cannot use is worse than an absence it can see.
                entry.Components = 0;
            }

            properties.push_back( std::move( entry ) );
        }

        return properties;
    }
} // namespace Desert::Editor::MaterialEdit
