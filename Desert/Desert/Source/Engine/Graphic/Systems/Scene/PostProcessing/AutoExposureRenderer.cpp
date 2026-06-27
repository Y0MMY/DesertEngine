#include "AutoExposureRenderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

#include <cstdint>

namespace Desert::Graphic::System
{
    namespace
    {
        constexpr uint32_t kBins        = 256;
        constexpr uint32_t kGroupSize   = 16; // matches AEHistogram local_size
        constexpr float    kMinLogLum   = -10.0f;
        constexpr float    kMaxLogLum   = 2.0f;
        constexpr float    kLogLumRange = kMaxLogLum - kMinLogLum;
        constexpr float    kLowPercent  = 0.0f;  // keep the darks (geometric-mean-like behaviour)
        constexpr float    kHighPercent = 0.95f; // discard the brightest 5% (sun/specular outliers)
        constexpr float    kDeltaTime   = 0.016f;

        struct HistogramPush
        {
            float MinLogLum;
            float InvLogLumRange;
        };

        struct AveragePush
        {
            float Dt;
            float AdaptSpeed;
            float MinLuma;
            float MaxLuma;
            float MinLogLum;
            float LogLumRange;
            float LowPct;
            float HighPct;
        };

        inline uint32_t GroupCount( uint32_t dim )
        {
            return ( dim + kGroupSize - 1 ) / kGroupSize;
        }
    } // namespace

    Common::BoolResultStr AutoExposureRenderer::Initialize()
    {
        if ( !CreateResources() )
            return Common::MakeError( "AutoExposureRenderer: failed to create compute resources" );
        return BOOLSUCCESS;
    }

    void AutoExposureRenderer::Shutdown()
    {
        m_ClearPipeline.reset();
        m_HistogramPipeline.reset();
        m_AveragePipeline.reset();
        m_LumImage[0].reset();
        m_LumImage[1].reset();
        m_Histogram.reset();
    }

    bool AutoExposureRenderer::CreateResources()
    {
        m_Histogram = ShaderResources::StorageBuffer::Create( "AEHistogram", kBins * sizeof( uint32_t ), 1 );

        const auto makeLum = [&]( const std::string& tag )
        {
            Core::Formats::Image2DSpecification spec = {
                 .Tag        = tag,
                 .Width      = 1,
                 .Height     = 1,
                 .Format     = Core::Formats::ImageFormat::RGBA32F,
                 .Mips       = 1u,
                 .Usage      = Core::Formats::Image2DUsage::Image2D,
                 .Properties = Core::Formats::Storage | Core::Formats::Sample,
            };
            return Image2D::Create( spec, nullptr );
        };
        m_LumImage[0] = makeLum( "AEAdaptedLum0" );
        m_LumImage[1] = makeLum( "AEAdaptedLum1" );

        const auto shaderService = Runtime::ResourceRegistry::GetShaderService();
        const auto make          = [&]( const std::string& name ) -> std::shared_ptr<ComputePipeline>
        {
            const auto shader = shaderService->GetByName( name );
            if ( !shader )
            {
                LOG_ERROR( "AutoExposureRenderer: missing compute shader '{}'", name );
                return nullptr;
            }
            auto pipeline = ComputePipeline::Create( { .Shader = shader, .DebugName = name } );
            pipeline->Invalidate();
            return pipeline;
        };
        m_ClearPipeline     = make( "AEHistogramClear" );
        m_HistogramPipeline = make( "AEHistogram" );
        m_AveragePipeline   = make( "AEAverage" );

        return m_Histogram && m_LumImage[0] && m_LumImage[1] && m_ClearPipeline && m_HistogramPipeline &&
               m_AveragePipeline;
    }

    void AutoExposureRenderer::Execute()
    {
        const auto& scene = m_TargetFramebuffer.lock();
        if ( !scene || !m_Histogram || !m_ClearPipeline || !m_HistogramPipeline || !m_AveragePipeline )
            return;

        Image2D* sceneColor = scene->GetColorAttachmentImage().get();
        if ( !sceneColor )
            return;

        const uint32_t sceneW = scene->GetFramebufferWidth();
        const uint32_t sceneH = scene->GetFramebufferHeight();

        const int prev  = m_ReadIndex;     // last frame's adapted luminance (sampled)
        const int write = 1 - m_ReadIndex; // new value written here
        Image2D*  prevLum = m_LumImage[prev].get();
        Image2D*  newLum  = m_LumImage[write].get();

        auto& renderer = Renderer::GetInstance();

        // newLum -> GENERAL + a graphics->compute barrier so the scene color (and prevLum) are visible.
        renderer.ComputeImageBeginWrite( newLum );

        // 1) Zero the histogram.
        m_ClearPipeline->SetStorageBuffer( 1, m_Histogram.get() );
        renderer.DispatchComputeInFrame( m_ClearPipeline.get(), 1, 1, 1 );

        // 2) Build the histogram from the full scene (one thread per texel, atomic adds).
        HistogramPush hp{ kMinLogLum, 1.0f / kLogLumRange };
        m_HistogramPipeline->SetInput( 0, sceneColor );
        m_HistogramPipeline->SetStorageBuffer( 1, m_Histogram.get() );
        m_HistogramPipeline->SetPushConstants( &hp, sizeof( hp ) );
        renderer.DispatchComputeInFrame( m_HistogramPipeline.get(), GroupCount( sceneW ), GroupCount( sceneH ),
                                         1 );

        // 3) Resolve: percentile-clipped weighted average + temporal adaptation -> newLum (1x1).
        AveragePush ap{ kDeltaTime,  m_AdaptSpeed,  m_MinLuma,    m_MaxLuma,
                        kMinLogLum,  kLogLumRange,  kLowPercent,  kHighPercent };
        m_AveragePipeline->SetStorageBuffer( 0, m_Histogram.get() );
        m_AveragePipeline->SetInput( 1, prevLum );
        m_AveragePipeline->SetOutput( 2, newLum, 0 );
        m_AveragePipeline->SetPushConstants( &ap, sizeof( ap ) );
        renderer.DispatchComputeInFrame( m_AveragePipeline.get(), 1, 1, 1 );

        // newLum -> SHADER_READ_ONLY so tonemap can sample it.
        renderer.ComputeImageEndWrite( newLum );

        m_ReadIndex = write; // the freshly written buffer is now the latest
    }
} // namespace Desert::Graphic::System
