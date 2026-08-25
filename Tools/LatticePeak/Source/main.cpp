// LatticePeak — the instrument that answers "is the sky on a grid", which neither ImageStat nor LineJump
// structurally can.
//
// WHY IT EXISTS. The owner of the product looked at the sky and said the clouds "just go in a row", that
// the whole sky is cloud "with an obvious pattern". Both instruments this programme already owns are
// content with a perfect lattice: ImageStat measures the DISTRIBUTION of luminance, and a grid of clouds
// and a natural field of the same clouds have the same histogram; LineJump measures the STEP between
// adjacent rows, and a grid whose period is longer than one row makes no step at all. So "it got better"
// would have been an opinion, and an opinion cannot be reviewed.
//
// WHAT IT MEASURES is in Source/LatticePeakMath.hpp beside this file, together with the reason the number
// reported is a PROMINENCE and not a correlation.
//
// TWO MODES, AND THE FIRST ONE IS THE ONE THAT CAN BE CHECKED.
//
//   --field   bakes the placement field itself through the shipped generator, projects it down and
//             measures the projection. This mode PREDICTS the period it expects — from the generator's own
//             CloudProceduralCellExtentKm, not from a number typed here again — and prints prediction and
//             measurement side by side. That is what makes the instrument checkable rather than merely
//             plausible: if a lattice of a known period does not produce a peak at that period, the
//             instrument is wrong and nothing measured with it counts.
//
//   --frame   measures a rendered PNG, which is what the owner is actually looking at. The mask is Otsu's
//             threshold on luminance, so the cloud/sky split is a property of the image rather than of
//             whoever ran the tool. This mode CANNOT predict a period: perspective maps one world period
//             onto a pixel period that shrinks with distance, so a real lattice arrives smeared, and the
//             peak it finds is a lower bound on the lattice's strength rather than a measurement of its
//             size. It is reported for the six protocol points because that is where the defect was seen;
//             the field mode is where the argument is settled.
//
// WHY --repeats EXISTS, AND IT IS THE FIRST THING THIS TOOL TAUGHT ITS AUTHOR. One 48 km region at the
// shipped 3 km cell holds sixteen cells across. An autocorrelation estimated from sixteen independent
// things wobbles by about a quarter of its own scale, and every wobble is a local maximum with a
// prominence of its own: the first run of this tool reported a "peak" of prominence 0.085 at 9.5 km, at a
// noise floor of 0.070, and there is no lattice at 9.5 km. Averaging the curve over several disjoint
// regions drops the floor as one over the square root of their number while leaving a real peak exactly
// where it was. The floor is printed on every line for the same reason.
//
// Usage:
//   LatticePeak --field [--region KM] [--tile KM] [--scale F] [--aniso F] [--coverage F] [--contrast F]
//               [--seed N] [--wind X,Z] [--chord KM] [--maxlag KM] [--repeats N] [--project sum|max]
//               [--density F] [--scatter F] [--variety F] [--patch F] [--patch-tile KM]
//               [--pgm PATH] [--csv PATH]
//   LatticePeak --frame <png> <x0> <y0> <x1> <y1> [<png> <x0> <y0> <x1> <y1> ...]

#include "LatticePeakMath.hpp"

#include <Engine/Assets/CloudProceduralVolume.hpp>
#include <Engine/Assets/CloudTypeData.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    using LatticePeak::Peak;

    /**
     * @brief The top-down projection of one species' channel.
     *
     * TWO PROJECTIONS AND THE DEFAULT IS THE SUM, because they answer different questions. The maximum
     * says only "is there cloud somewhere in this column", which saturates to one over most of a covered
     * sky and throws away exactly the amplitude a lattice modulates. The sum is how much cloud is in the
     * column — an optical thickness in all but units — and it is what a ray actually integrates, so it
     * keeps the variation the eye reads as a pattern.
     */
    std::vector<float> ProjectDown( const std::vector<unsigned char>& voxels, uint32_t slot, bool useSum )
    {
        const uint32_t width  = Desert::Assets::kCloudProceduralVolumeWidth;
        const uint32_t height = Desert::Assets::kCloudProceduralVolumeHeight;
        const uint32_t depth  = Desert::Assets::kCloudProceduralVolumeDepth;

        std::vector<float> map( static_cast<size_t>( width ) * depth, 0.0f );

        for ( uint32_t z = 0; z < depth; ++z )
            for ( uint32_t x = 0; x < width; ++x )
            {
                float value = 0.0f;
                for ( uint32_t y = 0; y < height; ++y )
                {
                    const size_t at = ( ( static_cast<size_t>( z ) * height + y ) * width + x ) *
                                      Desert::Assets::kCloudProceduralBytesPerVoxel;
                    const float sample = static_cast<float>( voxels[at + slot] ) * ( 1.0f / 255.0f );
                    value              = useSum ? value + sample : std::max( value, sample );
                }
                map[static_cast<size_t>( z ) * width + x] = useSum ? value / static_cast<float>( height ) : value;
            }

        return map;
    }

    void WritePgm( const char* path, const std::vector<float>& map, int width, int height, float scale )
    {
        std::FILE* file = std::fopen( path, "wb" );
        if ( !file )
        {
            std::fprintf( stderr, "LatticePeak: cannot write %s\n", path );
            return;
        }
        std::fprintf( file, "P5\n%d %d\n255\n", width, height );
        for ( float v : map )
        {
            const unsigned char byte =
                 static_cast<unsigned char>( std::clamp( v * scale, 0.0f, 1.0f ) * 255.0f + 0.5f );
            std::fwrite( &byte, 1, 1, file );
        }
        std::fclose( file );
    }

    /// How many multiples of the predicted period the lattice is looked for at. FOUR, because the first is
    /// the one a cluster as wide as its own cell hides — see LatticePeak::LatticeScore — and because the
    /// fourth is still inside the 16 km of lag the default window covers at the shipped 3 km cell.
    constexpr int kLatticeMultiples = 4;

    /// The field mode's report for one axis: every multiple of the predicted period, the noise the
    /// estimator makes on its own, and the headline the before/after table is written from.
    void ReportFieldAxis( const char* label, const std::vector<std::vector<double>>& curves, double unitsPerLag,
                          int predictedLag )
    {
        const std::vector<double> r     = LatticePeak::Average( curves );
        const int                 first = LatticePeak::FirstLagAfterCentralLobe( r );
        const double              noise = LatticePeak::JackknifeNoise( curves, first );

        const Peak strongest = LatticePeak::StrongestPeak( r );
        const Peak score     = LatticePeak::LatticeScore( r, predictedLag, kLatticeMultiples );

        std::printf( "  %-11s noise %6.4f   strongest bump ", label, noise );
        if ( strongest.Found )
            std::printf( "%7.3f km prom %6.4f\n", strongest.Lag * unitsPerLag, strongest.Prominence );
        else
            std::printf( "none (the curve decays and does not come back)\n" );

        if ( predictedLag <= 0 )
            return;

        for ( int m = 1; m <= kLatticeMultiples; ++m )
        {
            const Peak at = LatticePeak::PeakNear( r, predictedLag * m, std::max( 1, predictedLag / 8 ) );
            if ( at.Found )
                std::printf( "               %dP  predicted %7.3f km   found %7.3f km   r %+6.4f  "
                             "prom %6.4f  %5.1fx noise\n",
                             m, predictedLag * m * unitsPerLag, at.Lag * unitsPerLag, at.Value, at.Prominence,
                             noise > 1e-9 ? at.Prominence / noise : 0.0 );
            else
                std::printf( "               %dP  predicted %7.3f km   NO BUMP\n", m,
                             predictedLag * m * unitsPerLag );
        }

        if ( score.Found )
            std::printf( "               LATTICE %6.4f at %7.3f km (%dP), %.1fx noise\n", score.Prominence,
                         score.Lag * unitsPerLag, ( score.Lag + predictedLag / 2 ) / predictedLag,
                         noise > 1e-9 ? score.Prominence / noise : 0.0 );
        else
            std::printf( "               LATTICE 0.0000 — no bump on any multiple of the predicted period\n" );
    }

    /// The frame mode's report for one axis. No period can be predicted under perspective, so the only
    /// question that can be asked is where the strongest bump is; the answer is a LOWER BOUND on a lattice
    /// rather than a measurement of one, and the file note says why.
    void ReportFrameAxis( const char* label, const std::vector<double>& r )
    {
        const Peak best = LatticePeak::StrongestPeak( r );

        if ( !best.Found )
        {
            std::printf( "  %-11s no bump — the curve decays and does not come back\n", label );
            return;
        }

        std::printf( "  %-11s strongest bump %4d px   r %+6.3f   prom %6.4f\n", label, best.Lag, best.Value,
                     best.Prominence );
    }

    int FieldMode( int argc, char** argv )
    {
        float       regionKm = 48.0f;
        float       tileKm   = 12.0f;
        float       scale    = 1.0f;
        float       aniso    = 1.0f;
        float       coverage = 0.762f;
        float       contrast = 1.0f;
        float       chordKm  = 0.125f;
        float       maxLagKm = 16.0f;
        float       density  = -1.0f;
        float       scatter  = -1.0f;
        float       variety  = -1.0f;
        float       patchKm  = -1.0f;
        float       patch    = -1.0f;
        int         seed     = 1;
        int         repeats  = 8;
        bool        useSum   = true;
        float       windX    = 1.0f;
        float       windZ    = 0.0f;
        std::string pgm;
        std::string csv;

        for ( int i = 2; i < argc; ++i )
        {
            const std::string arg = argv[i];
            const bool        has = i + 1 < argc;

            if ( arg == "--region" && has )
                regionKm = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--tile" && has )
                tileKm = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--scale" && has )
                scale = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--aniso" && has )
                aniso = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--coverage" && has )
                coverage = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--contrast" && has )
                contrast = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--chord" && has )
                chordKm = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--maxlag" && has )
                maxLagKm = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--density" && has )
                density = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--scatter" && has )
                scatter = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--variety" && has )
                variety = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--patch-tile" && has )
                patchKm = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--patch" && has )
                patch = static_cast<float>( std::atof( argv[++i] ) );
            else if ( arg == "--seed" && has )
                seed = std::atoi( argv[++i] );
            else if ( arg == "--repeats" && has )
                repeats = std::max( 1, std::atoi( argv[++i] ) );
            else if ( arg == "--pgm" && has )
                pgm = argv[++i];
            else if ( arg == "--csv" && has )
                csv = argv[++i];
            else if ( arg == "--project" && has )
            {
                const std::string how = argv[++i];
                useSum                = ( how != "max" );
            }
            else if ( arg == "--wind" && has )
            {
                if ( std::sscanf( argv[++i], "%f,%f", &windX, &windZ ) != 2 )
                {
                    std::fprintf( stderr, "LatticePeak: --wind wants X,Z\n" );
                    return 2;
                }
            }
            else
            {
                std::fprintf( stderr, "LatticePeak: unknown field option %s\n", arg.c_str() );
                return 2;
            }
        }

        // THE SAME ARITHMETIC THE RENDERER PERFORMS, written here as VolumetricCloudRenderer::
        // BuildProceduralParams writes it: four cells to a weather tile.
        const float latticeKm = tileKm / 4.0f;

        Desert::Assets::CloudProceduralFieldParams params;
        params.RegionSizeKm      = regionKm;
        params.Coverage          = coverage;
        params.CoverageContrast  = contrast;
        params.Seed              = static_cast<uint32_t>( seed );
        params.WindAxis          = glm::vec2( windX, windZ );
        params.ResolvableChordKm = chordKm;
        params.BlendRadiusKm     = std::max( 0.02f * latticeKm, 1e-3f );
        params.ProfileDepthKm    = std::max( 0.12f * latticeKm, 1e-3f );

        // THE FOUR PLACEMENT KNOBS DEFAULT TO THE STRUCT'S OWN VALUES, so that running the tool with no
        // flags measures WHAT SHIPS rather than a configuration only the tool has. A negative on the
        // command line means "not given" for exactly that reason: zero is a legal setting of every one of
        // them and could not have carried the meaning.
        if ( density >= 0.0f )
            params.PlacementDensity = density;
        if ( scatter >= 0.0f )
            params.PlacementScatter = scatter;
        if ( variety >= 0.0f )
            params.PlacementSizeVariety = variety;
        if ( patchKm >= 0.0f )
            params.PatchTileKm = patchKm;
        if ( patch >= 0.0f )
            params.PatchStrength = patch;

        Desert::Assets::CloudProceduralSpecies species;
        species.Shape      = Desert::Assets::CloudTypeDefaultShape();
        species.CellKm     = latticeKm * std::max( scale, 1e-3f );
        species.Anisotropy = std::max( aniso, 1e-3f );

        params.LayerBottomKm    = species.Shape.BaseAltitudeKm;
        params.LayerThicknessKm = species.Shape.TopAltitudeKm - species.Shape.BaseAltitudeKm;
        params.Species.push_back( species );

        const int   width   = static_cast<int>( Desert::Assets::kCloudProceduralVolumeWidth );
        const int   depth   = static_cast<int>( Desert::Assets::kCloudProceduralVolumeDepth );
        const float voxelKm = regionKm / static_cast<float>( width );
        const int   maxLag  = std::min( width / 2, static_cast<int>( maxLagKm / voxelKm ) );

        // THE PREDICTION COMES FROM THE GENERATOR, not from this file's own arithmetic. A tool that
        // recomputed the cell would be checking its own multiplication rather than the sky's.
        const glm::vec2 extent = Desert::Assets::CloudProceduralCellExtentKm( params, params.Species[0] );

        std::vector<std::vector<double>> curvesX;
        std::vector<std::vector<double>> curvesZ;

        double cover = 0.0;
        size_t lumps = 0;

        for ( int repeat = 0; repeat < repeats; ++repeat )
        {
            // DISJOINT CELL SETS. Each realisation is the region a whole period further along, so no cell
            // is measured twice and the curves being averaged are independent — which is the only reason
            // averaging them lowers the floor at all.
            const float     cameraKm = static_cast<float>( repeat ) * regionKm * 4.0f;
            const glm::vec2 origin   = Desert::Assets::CloudProceduralRegionOriginKm( params, cameraKm, cameraKm );

            auto baked = Desert::Assets::BakeCloudProceduralVolume( params, origin );
            if ( !baked )
            {
                std::fprintf( stderr, "LatticePeak: the bake refused: %s\n", baked.GetError().c_str() );
                return 1;
            }

            const std::vector<float> map = ProjectDown( baked.GetValue(), 0u, useSum );

            if ( repeat == 0 )
            {
                if ( !pgm.empty() )
                    WritePgm( pgm.c_str(), map, width, depth, useSum ? 4.0f : 1.0f );
                lumps = Desert::Assets::CountCloudProceduralBlobs( params, origin );
            }

            double covered = 0.0;
            for ( float v : map )
                covered += ( v > 0.0f ) ? 1.0 : 0.0;
            cover += covered / static_cast<double>( map.size() );

            curvesX.push_back( LatticePeak::CircularAutocorrelation( map, width, depth, true, maxLag ) );
            curvesZ.push_back( LatticePeak::CircularAutocorrelation( map, width, depth, false, maxLag ) );
        }

        cover /= static_cast<double>( repeats );

        std::printf( "field  region %.1f km  voxel %.4f km  cell %.3f x %.3f km (predicted period)  "
                     "cover %.4f  lumps %zu  repeats %d  project %s\n",
                     regionKm, voxelKm, extent.x, extent.y, cover, lumps, repeats, useSum ? "sum" : "max" );

        // The lattice is laid out in the WIND's frame, so the map's two axes are the lattice's own axes
        // exactly when the wind runs along one of them. A rotated wind is reported as such rather than
        // silently compared against the wrong side of the cell.
        const bool axisAligned = ( std::fabs( windZ ) < 1e-6f ) || ( std::fabs( windX ) < 1e-6f );
        const bool swapped     = std::fabs( windX ) < 1e-6f;

        const int predictX =
             axisAligned ? static_cast<int>( ( swapped ? extent.y : extent.x ) / voxelKm + 0.5f ) : 0;
        const int predictZ =
             axisAligned ? static_cast<int>( ( swapped ? extent.x : extent.y ) / voxelKm + 0.5f ) : 0;

        if ( !axisAligned )
            std::printf( "  (wind %.3f,%.3f is not along a volume axis; no per-axis prediction is made)\n", windX,
                         windZ );

        const std::vector<double> rx = LatticePeak::Average( curvesX );
        const std::vector<double> rz = LatticePeak::Average( curvesZ );

        ReportFieldAxis( "X (east):", curvesX, voxelKm, predictX );
        ReportFieldAxis( "Z (north):", curvesZ, voxelKm, predictZ );

        if ( !csv.empty() )
        {
            std::FILE* file = std::fopen( csv.c_str(), "wb" );
            if ( file )
            {
                std::fprintf( file, "lag_km,r_x,r_z\n" );
                for ( size_t k = 0; k < rx.size(); ++k )
                    std::fprintf( file, "%.4f,%.6f,%.6f\n", k * voxelKm, rx[k], k < rz.size() ? rz[k] : 0.0 );
                std::fclose( file );
            }
            else
                std::fprintf( stderr, "LatticePeak: cannot write %s\n", csv.c_str() );
        }

        return 0;
    }

    int FrameMode( int argc, char** argv )
    {
        for ( int i = 2; i + 4 < argc; i += 5 )
        {
            const char* path = argv[i];
            const int   x0   = std::atoi( argv[i + 1] );
            const int   y0   = std::atoi( argv[i + 2] );
            int         x1   = std::atoi( argv[i + 3] );
            int         y1   = std::atoi( argv[i + 4] );

            int            w  = 0;
            int            h  = 0;
            int            ch = 0;
            unsigned char* px = stbi_load( path, &w, &h, &ch, 0 );
            if ( !px )
            {
                std::printf( "%-30s FAILED TO LOAD\n", path );
                continue;
            }

            x1 = std::min( x1, w );
            y1 = std::min( y1, h );

            const int width  = x1 - x0;
            const int height = y1 - y0;
            if ( width < 4 || height < 4 )
            {
                std::printf( "%-30s RECTANGLE TOO SMALL\n", path );
                stbi_image_free( px );
                continue;
            }

            std::vector<float> lum( static_cast<size_t>( width ) * height, 0.0f );
            for ( int y = 0; y < height; ++y )
                for ( int x = 0; x < width; ++x )
                {
                    const unsigned char* p = px + static_cast<size_t>( ( y + y0 ) * w + ( x + x0 ) ) * ch;
                    lum[static_cast<size_t>( y ) * width + x] = static_cast<float>(
                         0.2126 * p[0] / 255.0 + 0.7152 * p[1] / 255.0 + 0.0722 * p[2] / 255.0 );
                }

            const double threshold = LatticePeak::OtsuThreshold( lum );

            // THE MASK IS THE COVERAGE FIELD. Cloud is the BRIGHT class in every frame this programme
            // shoots — a lit cloud against sky — and the split is Otsu's, printed so a reader can see
            // which side of it the picture fell on.
            std::vector<float> mask( lum.size(), 0.0f );
            double             fraction = 0.0;
            for ( size_t k = 0; k < lum.size(); ++k )
            {
                mask[k] = ( lum[k] > threshold ) ? 1.0f : 0.0f;
                fraction += mask[k];
            }
            fraction /= static_cast<double>( mask.size() );

            std::string name  = path;
            const auto  slash = name.find_last_of( '/' );
            if ( slash != std::string::npos )
                name = name.substr( slash + 1 );

            std::printf( "%-30s  otsu %.3f  cloud %.4f\n", name.c_str(), threshold, fraction );

            const int maxLagX = std::min( width / 2, 512 );
            const int maxLagY = std::min( height / 2, 512 );

            ReportFrameAxis( "columns:",
                             LatticePeak::WindowedAutocorrelation( mask, width, height, true, maxLagX ) );
            ReportFrameAxis( "rows:",
                             LatticePeak::WindowedAutocorrelation( mask, width, height, false, maxLagY ) );

            stbi_image_free( px );
        }

        return 0;
    }

    int Usage()
    {
        std::fprintf(
             stderr,
             "usage: LatticePeak --field  [--region KM] [--tile KM] [--scale F] [--aniso F]\n"
             "                            [--coverage F] [--contrast F] [--seed N] [--wind X,Z]\n"
             "                            [--chord KM] [--maxlag KM] [--repeats N]\n"
             "                            [--density F] [--scatter F] [--variety F] [--patch F]\n"
             "                            [--patch-tile KM] [--project sum|max] [--pgm PATH] [--csv PATH]\n"
             "       LatticePeak --frame  <png> <x0> <y0> <x1> <y1> [<png> <x0> <y0> <x1> <y1> ...]\n"
             "\n"
             "  --field  bakes the placement field through the shipped generator and measures it; the\n"
             "           period it PREDICTS from CloudProceduralCellExtentKm is printed beside the one it\n"
             "           measures, which is how the instrument is checked\n"
             "  --frame  measures a rendered frame's cloud mask; no prediction is possible under\n"
             "           perspective, so the peak is a lower bound on the lattice rather than its size\n" );
        return 2;
    }
} // namespace

int main( int argc, char** argv )
{
    if ( argc < 2 )
        return Usage();

    const std::string mode = argv[1];
    if ( mode == "--field" )
        return FieldMode( argc, argv );
    if ( mode == "--frame" )
        return FrameMode( argc, argv );

    return Usage();
}
