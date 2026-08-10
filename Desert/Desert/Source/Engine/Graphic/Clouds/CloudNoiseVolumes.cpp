#include "CloudNoiseVolumes.hpp"

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Logger.hpp>

#include <algorithm>
#include <string>

namespace Desert::Graphic
{
    namespace
    {
        // The generation shaders. These names are what AssetPreloader registers the .shader files under
        // (the file's `Shader "Name"` header), so they are the contract between this file and
        // Editor/Resources/Shaders/Programs/Clouds/.
        constexpr const char* kShapeShaderName  = "CloudShapeNoise";
        constexpr const char* kDetailShaderName = "CloudDetailNoise";
        constexpr const char* kCurlShaderName   = "CloudCurlNoise";

        // The storage-image binding all three generation shaders declare. Bound EXPLICITLY here, so it
        // must equal the number in the shader: ComputePipeline::SetOutput takes the binding as an
        // argument and does not consult the shader's own reflection (the trap ParticleRenderer records —
        // a mismatch aliases another descriptor and fails as a validation error, not a wrong picture).
        constexpr uint32_t kNoiseOutputBinding = 0;

        // Mirrors the `PushConstant NoisePush { uint u_Seed; }` block in all three shaders. Dimensions
        // are NOT in here on purpose: the shaders read them with imageSize(), so there is one source of
        // truth for the volume's extent — a Dims push constant that disagreed with the actual image
        // would write a correct-looking volume with a wrong period, which is a seam nobody could trace.
        struct CloudNoisePushConstants
        {
            uint32_t Seed = 0;
        };

        // Vulkan guarantees at least 128 bytes of push-constant space; the engine's compute path emits a
        // single range of exactly this size.
        static_assert( sizeof( CloudNoisePushConstants ) <= 128,
                       "The cloud noise push block must fit the guaranteed push-constant range" );

        // Runs one generation dispatch. Returns false having logged the reason, so the caller only has to
        // decide what to do about it.
        bool DispatchNoise( const char* shaderName, Image* output, uint32_t seed, uint32_t groupsX,
                            uint32_t groupsY, uint32_t groupsZ )
        {
            const auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( shaderName );
            if ( !shader )
            {
                LOG_ERROR( "[CloudNoise] Compute shader '{}' is not registered — the volume cannot be "
                           "generated. Expected Editor/Resources/Shaders/Programs/Clouds/{}.shader.",
                           shaderName, shaderName );
                return false;
            }

            auto pipeline = ComputePipeline::Create( { .Shader = shader, .DebugName = shaderName } );
            if ( !pipeline )
            {
                LOG_ERROR( "[CloudNoise] Compute pipeline for '{}' could not be created.", shaderName );
                return false;
            }
            // Create() allocates the object; Invalidate() builds the Vulkan pipeline. Both are needed —
            // this two-line idiom is the same at every compute call site in the engine.
            pipeline->Invalidate();

            const CloudNoisePushConstants push{ .Seed = seed };

            pipeline->SetOutput( kNoiseOutputBinding, output, 0 );
            pipeline->SetPushConstants( &push, static_cast<uint32_t>( sizeof( push ) ) );
            pipeline->Dispatch( groupsX, groupsY, groupsZ );
            return true;
        }

        Image3DRef CreateVolume( const std::string& tag, uint32_t edge )
        {
            const Core::Formats::Image3DSpecification spec{
                 .Tag        = tag,
                 .Width      = edge,
                 .Height     = edge,
                 .Depth      = edge,
                 .Format     = kCloudNoiseFormat,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            return Image3D::Create( spec );
        }

        Image2DRef CreateCurlMap( const std::string& tag, uint32_t edge )
        {
            const Core::Formats::Image2DSpecification spec{
                 .Tag        = tag,
                 .Width      = edge,
                 .Height     = edge,
                 .Format     = kCloudNoiseFormat,
                 .Mips       = 1u,
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            return Image2D::Create( spec, nullptr );
        }

        constexpr uint32_t GroupCount( uint32_t extent )
        {
            return ( extent + kCloudNoiseWorkGroupSize - 1 ) / kCloudNoiseWorkGroupSize;
        }

        double BytesToMiB( uint64_t bytes )
        {
            return static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
        }
    } // namespace

    CloudNoiseVolumes& CloudNoiseVolumes::Get()
    {
        static CloudNoiseVolumes instance;
        return instance;
    }

    CloudNoiseVolumes::Entry* CloudNoiseVolumes::FindEntry( const CloudNoiseKey& key )
    {
        const auto it = std::find_if( m_Entries.begin(), m_Entries.end(),
                                      [&key]( const Entry& e ) { return e.Key == key; } );
        return it != m_Entries.end() ? &( *it ) : nullptr;
    }

    const CloudNoiseVolumes::Entry* CloudNoiseVolumes::FindEntry( const CloudNoiseKey& key ) const
    {
        const auto it = std::find_if( m_Entries.begin(), m_Entries.end(),
                                      [&key]( const Entry& e ) { return e.Key == key; } );
        return it != m_Entries.end() ? &( *it ) : nullptr;
    }

    const CloudNoiseSet* CloudNoiseVolumes::Find( const CloudNoiseKey& key ) const
    {
        const Entry* entry = FindEntry( key );
        if ( !entry || !entry->Volumes.IsComplete() )
            return nullptr;
        return &entry->Volumes;
    }

    void CloudNoiseVolumes::Acquire( const CloudNoiseKey& key, bool forceRegenerate )
    {
        Entry* entry = FindEntry( key );
        if ( !entry )
        {
            m_Entries.push_back( Entry{ .Key = key } );
            entry = &m_Entries.back();
        }

        ++entry->LeaseCount;

        const bool retryAfterFailure = forceRegenerate && entry->Failed;
        const bool rebuildExisting   = forceRegenerate && entry->Volumes.IsComplete();

        if ( entry->Volumes.IsComplete() && !rebuildExisting )
            return;
        if ( entry->Failed && !retryAfterFailure )
            return;

        if ( rebuildExisting )
        {
            // Dropping images that earlier frames may still have descriptors pointing at. The bake path
            // for the sky environment idles the device for exactly this reason before it swaps its cubes.
            Renderer::GetInstance().WaitDeviceIdle();
            entry->Volumes = CloudNoiseSet{};
        }

        entry->Failed = false;
        Generate( *entry );
    }

    void CloudNoiseVolumes::Release( const CloudNoiseKey& key )
    {
        const auto it = std::find_if( m_Entries.begin(), m_Entries.end(),
                                      [&key]( const Entry& e ) { return e.Key == key; } );
        if ( it == m_Entries.end() )
        {
            // A Release without a matching Acquire is a bookkeeping bug in the caller, and silently
            // ignoring it is how a set ends up leaked or freed twice. Name the key and carry on.
            LOG_ERROR( "[CloudNoise] Release of an unheld key (shape seed {}, detail seed {}).",
                       key.ShapeSeed, key.DetailSeed );
            return;
        }

        if ( it->LeaseCount == 0 )
        {
            LOG_ERROR( "[CloudNoise] Release of key (shape seed {}, detail seed {}) with no leases left.",
                       key.ShapeSeed, key.DetailSeed );
            return;
        }

        if ( --it->LeaseCount > 0 )
            return;

        const bool hadVolumes = it->Volumes.IsComplete();
        if ( hadVolumes )
        {
            // The images are destroyed as the entry is erased; the device may still be reading them from
            // a frame in flight.
            Renderer::GetInstance().WaitDeviceIdle();
            LOG_INFO( "[CloudNoise] Releasing the noise set for shape seed {} / detail seed {} — {:.2f} "
                      "MiB, no scene is using it any more.",
                      it->Key.ShapeSeed, it->Key.DetailSeed, BytesToMiB( CloudNoiseSetBytes() ) );
        }
        m_Entries.erase( it );
    }

    void CloudNoiseVolumes::Generate( Entry& entry )
    {
        const uint32_t curlSeed = CloudCurlSeedFrom( entry.Key.DetailSeed );

        CloudNoiseSet built;
        built.ShapeNoise =
             CreateVolume( "CloudShapeNoise_" + std::to_string( entry.Key.ShapeSeed ), kCloudShapeNoiseSize );
        built.DetailNoise = CreateVolume( "CloudDetailNoise_" + std::to_string( entry.Key.DetailSeed ),
                                          kCloudDetailNoiseSize );
        built.CurlNoise = CreateCurlMap( "CloudCurlNoise_" + std::to_string( curlSeed ), kCloudCurlNoiseSize );

        if ( !built.IsComplete() )
        {
            LOG_ERROR( "[CloudNoise] Allocation failed for shape seed {} / detail seed {} (shape {}, "
                       "detail {}, curl {}) — {:.2f} MiB requested. Clouds will not render.",
                       entry.Key.ShapeSeed, entry.Key.DetailSeed, static_cast<bool>( built.ShapeNoise ),
                       static_cast<bool>( built.DetailNoise ), static_cast<bool>( built.CurlNoise ),
                       BytesToMiB( CloudNoiseSetBytes() ) );
            entry.Failed = true;
            return;
        }

        const uint32_t volumeGroups = GroupCount( kCloudShapeNoiseSize );
        const uint32_t detailGroups = GroupCount( kCloudDetailNoiseSize );
        const uint32_t curlGroups   = GroupCount( kCloudCurlNoiseSize );

        const bool shapeOk = DispatchNoise( kShapeShaderName, built.ShapeNoise.get(), entry.Key.ShapeSeed,
                                            volumeGroups, volumeGroups, volumeGroups );
        const bool detailOk = DispatchNoise( kDetailShaderName, built.DetailNoise.get(), entry.Key.DetailSeed,
                                             detailGroups, detailGroups, detailGroups );
        const bool curlOk =
             DispatchNoise( kCurlShaderName, built.CurlNoise.get(), curlSeed, curlGroups, curlGroups, 1u );

        if ( !shapeOk || !detailOk || !curlOk )
        {
            // A partially filled set is worse than none: the raymarch would sample an uninitialised
            // volume and the picture would be wrong in a way that looks like a maths bug.
            LOG_ERROR( "[CloudNoise] Generation failed for shape seed {} / detail seed {} (shape {}, "
                       "detail {}, curl {}). The set is discarded and will not be retried until the "
                       "component asks for a regeneration.",
                       entry.Key.ShapeSeed, entry.Key.DetailSeed, shapeOk, detailOk, curlOk );
            entry.Failed = true;
            return;
        }

        entry.Volumes = std::move( built );

        LOG_INFO( "[CloudNoise] Generated the noise set for shape seed {} / detail seed {} (curl seed {}): "
                  "shape {}^3 + detail {}^3 + curl {}^2, RGBA8, {:.2f} MiB total.",
                  entry.Key.ShapeSeed, entry.Key.DetailSeed, curlSeed, kCloudShapeNoiseSize,
                  kCloudDetailNoiseSize, kCloudCurlNoiseSize, BytesToMiB( CloudNoiseSetBytes() ) );
    }
} // namespace Desert::Graphic
