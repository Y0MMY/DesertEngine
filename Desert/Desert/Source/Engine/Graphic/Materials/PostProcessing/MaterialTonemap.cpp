#include "MaterialTonemap.hpp"

#include <unordered_set>

namespace Desert::Graphic
{
    MaterialTonemap::MaterialTonemap() : Material( "MaterialTonemap", "SceneComposite" )
    {
        m_GeometryTexture = m_MaterialExecutor->GetTexture2DProperty( "u_GeometryTexture" ).get();
        m_BloomTexture    = m_MaterialExecutor->GetTexture2DProperty( "u_BloomTexture" ).get();
        m_AvgLuminance    = m_MaterialExecutor->GetTexture2DProperty( "u_AvgLuminance" ).get();
    }

    void MaterialTonemap::Bind( const std::shared_ptr<Image2D>& targetImage,
                                const std::shared_ptr<Image2D>& bloomImage,
                                const std::shared_ptr<Image2D>& avgLuminance, const Params& params )
    {
        if ( m_GeometryTexture && targetImage )
            m_GeometryTexture->SetImage( targetImage.get() );

        if ( m_BloomTexture && bloomImage )
            m_BloomTexture->SetImage( bloomImage.get() );

        if ( m_AvgLuminance && avgLuminance )
            m_AvgLuminance->SetImage( avgLuminance.get() );

        SetExposure( params.Exposure );
        SetGamma( params.Gamma );
        SetBloomIntensity( params.BloomIntensity );
        SetExposureKey( params.ExposureKey );
        SetAutoExposureEnabled( params.AutoExposure ? 1.0f : 0.0f );

        std::unordered_set<UniformBufferProperty*> dirtyUBs;
        UploadRegisteredProperties( dirtyUBs );

        // Flush EVERY UB with dirty fields (not just the ones touched this frame) so each per-frame-in-
        // flight copy receives the value — same reason as MaterialJFAComposite (TProperty::Set skips
        // unchanged values, which would otherwise leave other frame copies uninitialized → flicker).
        for ( const auto& [ubName, idx] : m_MaterialExecutor->GetUniformBufferProperties() )
        {
            auto ubProp = m_MaterialExecutor->GetUniformBufferProperty( ubName );
            if ( ubProp && ubProp->HasDirtyFields() )
                ubProp->UpdateFields();
        }
    }
} // namespace Desert::Graphic
