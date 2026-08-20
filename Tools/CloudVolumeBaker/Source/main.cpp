// CloudVolumeBaker — bakes a sculpted cloud body into a `.dcmv`.
//
// WHY A TOOL AND NOT A BUTTON. Phase A0 of Docs/Clouds/PLAN_AUTHORED_CLOUDS.md has no sculpting panel;
// that is A1. What A0 needs is a volume that exists, is reproducible and can be re-made when the
// generator's maths changes — which is a command line, not a UI. The baker survives A1: the panel will
// author the same `CloudModellingVolumeRecipe`, and this tool stays as the way to re-bake a library of
// them after a generator version bump.
//
// WHY IT IS A FILE AND NOT GENERATED AT LOAD. 1 048 576 voxels times eight lumps is eight million
// ellipsoid evaluations plus their exponentials, and a debug build spends about a second on it. That is
// a stall on every launch of every scene with a hero cloud in it, for an answer that never changes. The
// same decision `.dcnv` records on kCloudNoiseDefaultVolumeName, for the same reason.
//
//   CloudVolumeBaker --out <path.dcmv> [--in <path.dcmv>]
//
// With `--in` the recipe is read out of an existing volume's header and re-baked — which is what makes a
// generator version bump a mechanical operation over a directory rather than a re-authoring. Without it
// the shipped example recipe is baked, which is the file the demo scene names.

#include <Engine/Assets/CloudModellingVolume.hpp>
#include <Engine/Assets/CloudModellingVolumeAsset.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    int Usage()
    {
        std::fprintf( stderr, "usage: CloudVolumeBaker --out <path.dcmv> [--in <path.dcmv>]\n"
                              "  --out  where to write the baked volume\n"
                              "  --in   re-bake the recipe stored in an existing volume instead of the\n"
                              "         engine's shipped example\n" );
        return 2;
    }
} // namespace

int main( int argc, char** argv )
{
    std::string outPath;
    std::string inPath;

    for ( int i = 1; i < argc; ++i )
    {
        const std::string arg = argv[i];
        if ( arg == "--out" && i + 1 < argc )
            outPath = argv[++i];
        else if ( arg == "--in" && i + 1 < argc )
            inPath = argv[++i];
        else
            return Usage();
    }

    if ( outPath.empty() )
        return Usage();

    Desert::Assets::CloudModellingVolumeRecipe recipe = Desert::Assets::CloudModellingDefaultRecipe();

    if ( !inPath.empty() )
    {
        std::ifstream file( inPath, std::ios::binary );
        if ( !file )
        {
            std::fprintf( stderr, "CloudVolumeBaker: '%s' could not be opened\n", inPath.c_str() );
            return 1;
        }

        const std::vector<unsigned char> bytes( ( std::istreambuf_iterator<char>( file ) ),
                                                std::istreambuf_iterator<char>() );

        const auto decoded = Desert::Assets::DecodeCloudModellingVolume( bytes );
        if ( !decoded )
        {
            std::fprintf( stderr, "CloudVolumeBaker: '%s' is not usable: %s\n", inPath.c_str(),
                          decoded.GetError().c_str() );
            return 1;
        }

        recipe = decoded.GetValue().Recipe;
        std::printf( "Re-baking the recipe stored in '%s': %zu lumps.\n", inPath.c_str(), recipe.Blobs.size() );
    }

    const auto started = std::chrono::steady_clock::now();

    auto voxels = Desert::Assets::GenerateCloudModellingVolume( recipe );
    if ( !voxels )
    {
        std::fprintf( stderr, "CloudVolumeBaker: the volume could not be baked: %s\n", voxels.GetError().c_str() );
        return 1;
    }

    const auto elapsed =
         std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - started );

    Desert::Assets::CloudModellingVolumeData data;
    data.Recipe = recipe;
    data.Voxels = voxels.ExtractValue();

    // WRITTEN HERE RATHER THAN THROUGH CloudModellingVolumeAsset::Save, because that function logs through
    // the engine's logger and this tool links no engine. Same encoder, same validation — the container is
    // a pure function of the data, which is exactly why it was written as one.
    if ( const auto valid = Desert::Assets::ValidateCloudModellingRecipe( data.Recipe ); !valid )
    {
        std::fprintf( stderr, "CloudVolumeBaker: %s\n", valid.GetError().c_str() );
        return 1;
    }

    const std::vector<unsigned char> encoded = Desert::Assets::EncodeCloudModellingVolume( data );

    std::ofstream out( outPath, std::ios::binary | std::ios::trunc );
    if ( !out )
    {
        std::fprintf( stderr, "CloudVolumeBaker: '%s' could not be opened for writing\n", outPath.c_str() );
        return 1;
    }

    out.write( reinterpret_cast<const char*>( encoded.data() ), static_cast<std::streamsize>( encoded.size() ) );
    if ( !out )
    {
        std::fprintf( stderr, "CloudVolumeBaker: '%s' was opened but the %zu bytes could not be written\n",
                      outPath.c_str(), encoded.size() );
        return 1;
    }

    // The numbers a re-bake has to be judged by: the box, the voxel, and how much of the volume is body.
    // A volume whose occupancy is one per cent is a cloud lost in a box far too big for it, and that is
    // not visible from the recipe.
    size_t occupied = 0;
    for ( size_t i = 0; i < data.Voxels.size(); i += 4 )
    {
        if ( data.Voxels[i] > 0 )
            ++occupied;
    }

    const double voxelCount = static_cast<double>( data.Voxels.size() / 4 );

    std::printf( "Wrote '%s': %zu bytes, %zu lumps, %.2f x %.2f x %.2f km, voxel %.1f x %.1f x %.1f m, "
                 "%.1f%% of voxels carry body, baked in %lld ms.\n",
                 outPath.c_str(), encoded.size(), recipe.Blobs.size(), recipe.SizeKm.x, recipe.SizeKm.y,
                 recipe.SizeKm.z,
                 recipe.SizeKm.x * 1000.0f / static_cast<float>( Desert::Assets::kCloudModellingVolumeWidth ),
                 recipe.SizeKm.y * 1000.0f / static_cast<float>( Desert::Assets::kCloudModellingVolumeHeight ),
                 recipe.SizeKm.z * 1000.0f / static_cast<float>( Desert::Assets::kCloudModellingVolumeDepth ),
                 100.0 * static_cast<double>( occupied ) / voxelCount, static_cast<long long>( elapsed.count() ) );

    return 0;
}
