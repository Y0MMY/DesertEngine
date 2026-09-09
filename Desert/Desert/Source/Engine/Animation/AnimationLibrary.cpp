#include "AnimationLibrary.hpp"

#include <Engine/Animation/ProceduralCharacterAnimations.hpp>
#include <Engine/Animation/Skeleton.hpp>

#include <Common/Core/Logger.hpp>

#include <algorithm>

namespace Desert::Animation
{
    AnimationLibrary::AnimationLibrary( Assets::AssetManager* assetManager ) : m_AssetManager( assetManager )
    {
    }

    // RELOAD BEFORE HANDING ONE OUT. The lookup resolves a handle the record kept at Register time, and an
    // evicted clip resolves to a perfectly valid asset holding an empty track list — so without this the
    // caller gets a successful answer that animates nothing. AssetBase::EnsureLoaded is a no-op for a clip
    // that is already resident, which is every clip in the common case.
    //
    // A clip that CANNOT be reloaded is named and skipped rather than returned empty: a procedural clip
    // (SetInMemoryClip) is never evicted, so reaching this branch means the file is gone.
    Assets::Asset<Assets::AnimationAsset> AnimationLibrary::Resolve( const Assets::AssetHandle& handle ) const
    {
        auto asset = m_AssetManager->FindByHandle<Assets::AnimationAsset>( handle );
        if ( !asset )
            return nullptr;

        if ( const auto loaded = asset->EnsureLoaded( *m_AssetManager ); !loaded )
        {
            LOG_ERROR( "[AnimationLibrary] clip '{}' is indexed for a rig but could not be loaded: {}. It is "
                       "not offered; a caller given it would have played an empty clip.",
                       asset->GetMetadata().Filepath.string(), loaded.GetError() );
            return nullptr;
        }

        return asset;
    }

    void AnimationLibrary::Register( const Assets::Asset<Assets::AnimationAsset>& animation )
    {
        if ( !animation )
        {
            return;
        }

        ClipRigIdentity identity;
        identity.Handle            = animation->GetMetadata().Handle;
        identity.ClipName          = animation->GetClip().AnimationName;
        identity.SkeletonSignature = animation->GetSkeletonSignature();
        for ( const auto& track : animation->GetClip().Tracks )
            if ( !track.BoneName.empty() )
                identity.AnimatedBones.push_back( track.BoneName );

        // A clip with neither a rig signature nor one named bone can never match anything — ClipDrivesRig
        // has nothing to test it on. Registering it silently is how a clip becomes invisible with no way to
        // tell that from "the project has no clips".
        if ( identity.SkeletonSignature == 0 && identity.AnimatedBones.empty() )
        {
            LOG_ERROR( "[AnimationLibrary] clip '{}' ({}) claims no rig and animates no named bone, so no "
                       "skeleton can ever match it. It is registered and will never be offered.",
                       identity.ClipName, animation->GetMetadata().Filepath.string() );
        }

        m_Clips.push_back( std::move( identity ) );
    }

    void AnimationLibrary::Unregister( const Assets::AssetHandle& handle )
    {
        m_Clips.erase( std::remove_if( m_Clips.begin(), m_Clips.end(),
                                       [&]( const ClipRigIdentity& c ) { return c.Handle == handle; } ),
                       m_Clips.end() );
    }

    std::vector<Assets::Asset<Assets::AnimationAsset>>
    AnimationLibrary::GetForSkeleton( const Skeleton& skeleton ) const
    {
        const RigIdentity rig = IdentifyRig( skeleton );

        std::vector<Assets::Asset<Assets::AnimationAsset>> result;
        for ( const size_t i : SelectClipsForRig( m_Clips, rig ) )
        {
            if ( auto asset = Resolve( m_Clips[i].Handle ) )
                result.push_back( asset );
        }
        return result;
    }

    Common::ResultStr<Assets::Asset<Assets::AnimationAsset>>
    AnimationLibrary::FindForSkeleton( const Skeleton& skeleton, const std::string& clipName ) const
    {
        const RigIdentity rig = IdentifyRig( skeleton );

        const auto index = FindClipForRig( m_Clips, rig, clipName );
        if ( !index )
            return Common::MakeError<Assets::Asset<Assets::AnimationAsset>>( index.GetError() );

        auto asset = Resolve( m_Clips[index.GetValue()].Handle );
        if ( !asset )
        {
            // Resolve already logged the reason; this turns it into a refusal the caller must handle rather
            // than a null it can drop on the floor.
            return Common::MakeFormattedError<Assets::Asset<Assets::AnimationAsset>>(
                 "clip '{}' drives this rig but its asset could not be resolved or reloaded.", clipName );
        }
        return Common::MakeSuccess( std::move( asset ) );
    }

    void AnimationLibrary::Clear()
    {
        m_Clips.clear();
    }

    Common::ResultStr<LibraryPopulation> PopulateLibrary( Assets::AssetManager& assets, AnimationLibrary& library,
                                                          const size_t clipFilesDiscovered )
    {
        // CLEARED FIRST because this is also the re-scan path: `AssetPreloader::ReloadCooked` ("Rebuild
        // Cooked Assets") runs the whole discovery again, and a library that only ever grew would answer
        // with two records per clip afterwards — the second of which resolves the same handle, so nothing
        // would look wrong until a picker showed every clip twice.
        library.Clear();

        LibraryPopulation counts;

        for ( const auto& [handle, animation] : assets.FindAllByType<Assets::AnimationAsset>() )
        {
            if ( !animation )
                continue;

            // The in-memory locomotion clips are in the manager too (RegisterClips creates them as assets
            // so the pickers can offer them like any other), and they are registered below rather than
            // here. Skipping them by "was there a file behind this?" is the same question the eviction
            // path asks, and it is the only property that distinguishes them.
            if ( !animation->IsReloadableFromFile() )
                continue;

            library.Register( animation );
            ++counts.FromFiles;
        }

        counts.Procedural = ProceduralCharacterAnimations::RegisterClips( assets, library );

        LOG_INFO( "[AnimationLibrary] {} clip(s) registered: {} from {} `.anim` file(s) on disk, {} built-in "
                  "procedural.",
                  counts.FromFiles + counts.Procedural, counts.FromFiles, clipFilesDiscovered, counts.Procedural );

        // THE CASE THAT SHIPPED, said out loud. An empty library is the correct state for a project with no
        // clips and a broken one for a project with clips on disk, and only the scan's own count can tell
        // the two apart — which is why it is a parameter. Without this line the symptom is a character
        // standing still and no log line anywhere in the process.
        if ( clipFilesDiscovered > 0 && counts.FromFiles == 0 )
        {
            return Common::MakeFormattedError<LibraryPopulation>(
                 "the asset scan found {} `.anim` file(s) under the cooked mesh root and NOT ONE of them "
                 "reached the animation library. Every skinned character whose clip comes from a file will "
                 "stand in its bind pose. {} built-in procedural clip(s) are registered, so a library that "
                 "answers at all is not evidence the files arrived.",
                 clipFilesDiscovered, counts.Procedural );
        }

        // Fewer than were found is a real loss too — a file that failed to parse never became an asset —
        // but it is a partial one, and the per-file reason is already on the log from AnimationAsset::Load.
        if ( counts.FromFiles < clipFilesDiscovered )
        {
            LOG_WARN( "[AnimationLibrary] {} of {} `.anim` file(s) did not become a clip asset and are not "
                      "in the library; the reason for each is logged above by the asset load that failed.",
                      clipFilesDiscovered - counts.FromFiles, clipFilesDiscovered );
        }

        return Common::MakeSuccess( counts );
    }
} // namespace Desert::Animation
