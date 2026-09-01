#include <Editor/Panels/FileExplorer/NewCloudAsset.hpp>

#include <Engine/Assets/CloudNoiseVolumeGenerator.hpp>

namespace Desert::Editor::NewCloudAsset
{
    Assets::CloudTypeData DefaultType( std::string_view displayName )
    {
        Assets::CloudTypeData data = Assets::CloudTypeDefault();

        data.DisplayName = std::string( displayName );

        // CLEARED RATHER THAN REWRITTEN. The built-in's note describes the built-in ("The type an empty
        // slot resolves to. It is not a file"), and a file is exactly what this is about to become;
        // inventing a replacement here would put this panel's prose into every asset an artist creates.
        // The field is optional precisely so that "nobody has written a note yet" can be said.
        data.Notes.reset();

        return data;
    }

    Common::ResultStr<Assets::CloudLayoutData> DefaultLayout()
    {
        auto canvas = Assets::MakeCloudLayoutCanvas( kNewLayoutSide );
        if ( !canvas )
            return Common::MakeFormattedError<Assets::CloudLayoutData>( "a blank {}x{} canvas could not be "
                                                                        "made: {}",
                                                                        kNewLayoutSide, kNewLayoutSide,
                                                                        canvas.GetError() );

        const Assets::CloudLayoutCanvas& blank = canvas.GetValue();

        // STRAIGHT RGBA AND NO MASK — CloudLayoutPanel's own defaults, and the only mapping that means
        // anything for a canvas nobody has painted on. `TakeMask` is carried from the canvas rather than
        // written as `false` here, because the canvas is what knows whether its alpha plane is a mask: a
        // blank one says no, since its alpha is the mask's NEUTRAL and a uniformly neutral mask is a table
        // carried for no reason.
        const uint32_t channelForSlot[Assets::kCloudLayoutChannels] = { 0u, 1u, 2u, 3u };

        return Assets::MakeCloudLayoutFromImage( blank.Pixels, blank.Side, blank.Side, channelForSlot,
                                                 blank.TakeMask );
    }

    Assets::CloudNoiseVolumeParams DefaultNoiseParams()
    {
        return Assets::CloudNoiseVolumeParams{};
    }

    Common::ResultStr<Assets::CloudNoiseVolumeData> DefaultNoiseVolume( std::atomic<float>* progress )
    {
        return Assets::GenerateCloudNoiseVolume( DefaultNoiseParams(), progress );
    }

    Common::ResultStr<Assets::CloudModellingVolumeData>
    DefaultModellingVolume( const Assets::CloudModellingBakeProgressFn& onProgress )
    {
        Assets::CloudModellingVolumeData data;
        data.Recipe = Assets::CloudModellingDefaultRecipe();

        auto voxels = Assets::GenerateCloudModellingVolume( data.Recipe, onProgress );
        if ( !voxels )
            return Common::MakeFormattedError<Assets::CloudModellingVolumeData>(
                 "the shipped example body could not be baked: {}", voxels.GetError() );

        data.Voxels = voxels.ExtractValue();
        return Common::MakeSuccess( std::move( data ) );
    }
} // namespace Desert::Editor::NewCloudAsset
