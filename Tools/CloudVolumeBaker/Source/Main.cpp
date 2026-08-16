// CloudVolumeBaker — standalone CLI over the ENGINE's hero-cloud baker
// (Engine/Graphic/Clouds/CloudVolumeBake.hpp, compiled straight in, exactly as DShaderTool compiles
// the shader parser). Turns an authored list of analytic primitives into a `.dvol` volume, and dumps
// a slice of one as a PNG so a bake can be LOOKED AT on a machine with no GPU.
//
//   CloudVolumeBaker bake  <shape.json> <out.dvol>            bake a shape description into a volume
//   CloudVolumeBaker info  <volume.dvol>                       print the header and the channel ranges
//   CloudVolumeBaker slice <volume.dvol> <out.png> [options]   write one cross-section as a PNG
//
//   slice options:
//     --plane xz|xy|yz   which cross-section (default xz — the VERTICAL one, since local Z is up)
//     --index N          the slice, along the remaining axis (default: the middle)
//     --channel profile|type|scale|distance|rgb   what to draw (default profile)
//     --scale N          integer magnification, so a 128x64 slice is legible (default 4)
//
// WHY THE SHAPES ARE NOT IN HERE. A cumulus, a congestus tower and an anvil are CONTENT. They live in
// `.cloudshape.json` files under Editor/Resources/Assets/Clouds/Shapes, and a fourth one is authoring
// rather than a rebuild. The tool knows about primitives; it knows nothing about clouds.

#include <Engine/Graphic/Clouds/CloudVolumeBake.hpp>
#include <Engine/Graphic/Clouds/CloudVolumeFormat.hpp>

#include <rflcpp/rfl/json.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image/stb_image_write.h>

namespace fs = std::filesystem;

using Desert::Graphic::CloudBakeDescription;
using Desert::Graphic::CloudVolume;

namespace
{
    int Usage()
    {
        std::fprintf( stderr,
                      "CloudVolumeBaker — bake analytic cloud primitives into a .dvol volume\n"
                      "\n"
                      "  CloudVolumeBaker bake  <shape.json> <out.dvol>\n"
                      "  CloudVolumeBaker info  <volume.dvol>\n"
                      "  CloudVolumeBaker slice <volume.dvol> <out.png> [--plane xz|xy|yz] [--index N]\n"
                      "                         [--channel profile|type|scale|distance|rgb] [--scale N]\n" );
        return 2;
    }

    bool ReadWholeFile( const fs::path& path, std::string& out )
    {
        std::ifstream stream( path, std::ios::binary );
        if ( !stream )
            return false;

        stream.seekg( 0, std::ios::end );
        const std::streamoff size = stream.tellg();
        stream.seekg( 0, std::ios::beg );

        out.resize( static_cast<size_t>( size ) );
        stream.read( out.data(), size );
        return static_cast<bool>( stream );
    }

    bool LoadVolume( const fs::path& path, CloudVolume& out )
    {
        std::string raw;
        if ( !ReadWholeFile( path, raw ) )
        {
            std::fprintf( stderr, "CloudVolumeBaker: cannot read %s\n", path.string().c_str() );
            return false;
        }

        auto parsed =
             Desert::Graphic::ReadCloudVolume( reinterpret_cast<const unsigned char*>( raw.data() ), raw.size() );
        if ( !parsed.IsSuccess() )
        {
            std::fprintf( stderr, "CloudVolumeBaker: %s: %s\n", path.string().c_str(), parsed.GetError().c_str() );
            return false;
        }

        out = parsed.GetValue();
        return true;
    }

    int Bake( const fs::path& descriptionPath, const fs::path& volumePath )
    {
        std::string json;
        if ( !ReadWholeFile( descriptionPath, json ) )
        {
            std::fprintf( stderr, "CloudVolumeBaker: cannot read %s\n", descriptionPath.string().c_str() );
            return 1;
        }

        // DefaultIfMissing so a description written before a field existed keeps loading — the same rule
        // every other authored file in this engine is read under.
        auto description = rfl::json::read<CloudBakeDescription, rfl::DefaultIfMissing>( json );
        if ( !description )
        {
            const std::string reason = description.error().what();
            std::fprintf( stderr, "CloudVolumeBaker: %s is not a valid shape description: %s\n",
                          descriptionPath.string().c_str(), reason.c_str() );
            return 1;
        }

        const auto baked = Desert::Graphic::BakeCloudVolume( description->Shape, description->Settings );
        if ( !baked.IsSuccess() )
        {
            std::fprintf( stderr, "CloudVolumeBaker: %s: %s\n", descriptionPath.string().c_str(),
                          baked.GetError().c_str() );
            return 1;
        }

        const auto bytes = Desert::Graphic::WriteCloudVolume( baked.GetValue() );
        if ( !bytes.IsSuccess() )
        {
            std::fprintf( stderr, "CloudVolumeBaker: %s\n", bytes.GetError().c_str() );
            return 1;
        }

        if ( volumePath.has_parent_path() && !volumePath.parent_path().empty() )
        {
            std::error_code ec;
            fs::create_directories( volumePath.parent_path(), ec );
        }

        std::ofstream stream( volumePath, std::ios::binary | std::ios::trunc );
        if ( !stream )
        {
            std::fprintf( stderr, "CloudVolumeBaker: cannot write %s\n", volumePath.string().c_str() );
            return 1;
        }
        stream.write( reinterpret_cast<const char*>( bytes.GetValue().data() ),
                      static_cast<std::streamsize>( bytes.GetValue().size() ) );
        if ( !stream )
        {
            std::fprintf( stderr, "CloudVolumeBaker: write failed for %s\n", volumePath.string().c_str() );
            return 1;
        }

        const auto& header = baked.GetValue().Header;
        std::printf( "CloudVolumeBaker: baked '%s' from %zu primitive(s) into %s\n"
                     "  %ux%ux%u RGBA8, %.2f MiB, covering %.0f x %.0f x %.0f m "
                     "(%.2f x %.2f x %.2f m per voxel)\n",
                     description->Name.c_str(), description->Shape.Primitives.size(), volumePath.string().c_str(),
                     header.Width, header.Height, header.Depth,
                     static_cast<double>( bytes.GetValue().size() ) / ( 1024.0 * 1024.0 ), header.ExtentX / 100.0f,
                     header.ExtentY / 100.0f, header.ExtentZ / 100.0f,
                     header.ExtentX / 100.0f / static_cast<float>( header.Width ),
                     header.ExtentY / 100.0f / static_cast<float>( header.Height ),
                     header.ExtentZ / 100.0f / static_cast<float>( header.Depth ) );
        return 0;
    }

    int Info( const fs::path& volumePath )
    {
        CloudVolume volume;
        if ( !LoadVolume( volumePath, volume ) )
            return 1;

        const auto& header = volume.Header;

        unsigned char channelMin[4] = { 255, 255, 255, 255 };
        unsigned char channelMax[4] = { 0, 0, 0, 0 };
        size_t        occupied      = 0;
        for ( size_t i = 0; i < volume.Voxels.size(); i += 4 )
        {
            for ( size_t c = 0; c < 4; ++c )
            {
                channelMin[c] = volume.Voxels[i + c] < channelMin[c] ? volume.Voxels[i + c] : channelMin[c];
                channelMax[c] = volume.Voxels[i + c] > channelMax[c] ? volume.Voxels[i + c] : channelMax[c];
            }
            if ( volume.Voxels[i] > 0 )
                ++occupied;
        }

        const size_t voxels = volume.Voxels.size() / 4;
        std::printf(
             "%s\n"
             "  version %u, layout %u, %ux%ux%u RGBA8 (%.2f MiB)\n"
             "  extent  %.1f x %.1f x %.1f m  (%.2f m per voxel on X)\n"
             "  sdf range +/- %.1f m, %.2f m per quantisation level\n"
             "  profile  [%u..%u]   detail type [%u..%u]   density scale [%u..%u]   sdf [%u..%u]\n"
             "  %zu of %zu voxels carry a non-zero profile (%.1f%%)\n",
             volumePath.string().c_str(), header.Version, header.ChannelLayout, header.Width, header.Height,
             header.Depth, static_cast<double>( volume.Voxels.size() ) / ( 1024.0 * 1024.0 ),
             header.ExtentX / 100.0f, header.ExtentY / 100.0f, header.ExtentZ / 100.0f,
             header.ExtentX / 100.0f / static_cast<float>( header.Width ), header.SignedDistanceRange / 100.0f,
             header.SignedDistanceRange / 100.0f / 127.0f, channelMin[0], channelMax[0], channelMin[1],
             channelMax[1], channelMin[2], channelMax[2], channelMin[3], channelMax[3], occupied, voxels,
             100.0 * static_cast<double>( occupied ) / static_cast<double>( voxels ) );
        return 0;
    }

    // Which of the three axes the requested plane holds fixed, and which two it spans.
    struct SlicePlane
    {
        int Horizontal = 0; // the volume axis drawn left-to-right
        int Vertical   = 2; // the volume axis drawn bottom-to-top
        int Fixed      = 1; // the axis the slice index runs along
    };

    bool ParsePlane( const std::string& name, SlicePlane& out )
    {
        if ( name == "xz" )
        {
            out = SlicePlane{ 0, 2, 1 };
            return true;
        }
        if ( name == "xy" )
        {
            out = SlicePlane{ 0, 1, 2 };
            return true;
        }
        if ( name == "yz" )
        {
            out = SlicePlane{ 1, 2, 0 };
            return true;
        }
        return false;
    }

    int Slice( const fs::path& volumePath, const fs::path& imagePath, const SlicePlane& plane, int index,
               const std::string& channel, int magnification )
    {
        CloudVolume volume;
        if ( !LoadVolume( volumePath, volume ) )
            return 1;

        const uint32_t dims[3] = { volume.Header.Width, volume.Header.Height, volume.Header.Depth };

        if ( index < 0 )
            index = static_cast<int>( dims[plane.Fixed] ) / 2;

        if ( index >= static_cast<int>( dims[plane.Fixed] ) )
        {
            std::fprintf( stderr, "CloudVolumeBaker: slice %d is outside the %u the volume has on that axis\n",
                          index, dims[plane.Fixed] );
            return 1;
        }

        int channelIndex = 0;
        if ( channel == "profile" )
            channelIndex = 0;
        else if ( channel == "type" )
            channelIndex = 1;
        else if ( channel == "scale" )
            channelIndex = 2;
        else if ( channel == "distance" )
            channelIndex = 3;
        else if ( channel == "rgb" )
            channelIndex = -1;
        else
        {
            std::fprintf( stderr, "CloudVolumeBaker: unknown channel '%s'\n", channel.c_str() );
            return 1;
        }

        const uint32_t width  = dims[plane.Horizontal];
        const uint32_t height = dims[plane.Vertical];
        const int      scale  = magnification < 1 ? 1 : magnification;

        std::vector<unsigned char> pixels( static_cast<size_t>( width ) * height * scale * scale * 3, 0 );

        for ( uint32_t v = 0; v < height; ++v )
        {
            for ( uint32_t h = 0; h < width; ++h )
            {
                uint32_t coordinate[3]       = { 0, 0, 0 };
                coordinate[plane.Horizontal] = h;
                coordinate[plane.Vertical]   = v;
                coordinate[plane.Fixed]      = static_cast<uint32_t>( index );

                const size_t at = Desert::Graphic::CloudVolumeVoxelIndex( volume.Header, coordinate[0],
                                                                          coordinate[1], coordinate[2] );

                unsigned char rgb[3];
                if ( channelIndex < 0 )
                {
                    rgb[0] = volume.Voxels[at + 0];
                    rgb[1] = volume.Voxels[at + 1];
                    rgb[2] = volume.Voxels[at + 2];
                }
                else
                {
                    rgb[0] = rgb[1] = rgb[2] = volume.Voxels[at + static_cast<size_t>( channelIndex )];
                }

                // Row 0 of a PNG is the TOP, and the vertical axis of the slice increases upward, so the
                // rows are written in reverse. Getting this wrong prints an upside-down anvil, which is a
                // recognisable enough shape that it would be believed.
                const size_t row = static_cast<size_t>( height - 1 - v );
                for ( int sy = 0; sy < scale; ++sy )
                {
                    for ( int sx = 0; sx < scale; ++sx )
                    {
                        const size_t px = ( ( row * scale + sy ) * width * scale + h * scale + sx ) * 3;
                        pixels[px + 0]  = rgb[0];
                        pixels[px + 1]  = rgb[1];
                        pixels[px + 2]  = rgb[2];
                    }
                }
            }
        }

        if ( imagePath.has_parent_path() && !imagePath.parent_path().empty() )
        {
            std::error_code ec;
            fs::create_directories( imagePath.parent_path(), ec );
        }

        if ( stbi_write_png( imagePath.string().c_str(), static_cast<int>( width ) * scale,
                             static_cast<int>( height ) * scale, 3, pixels.data(),
                             static_cast<int>( width ) * scale * 3 ) == 0 )
        {
            std::fprintf( stderr, "CloudVolumeBaker: cannot write %s\n", imagePath.string().c_str() );
            return 1;
        }

        std::printf( "CloudVolumeBaker: wrote %s — %s channel, slice %d, %ux%u magnified %dx\n",
                     imagePath.string().c_str(), channel.c_str(), index, width, height, scale );
        return 0;
    }
} // namespace

int main( int argc, char** argv )
{
    if ( argc < 3 )
        return Usage();

    const std::string command = argv[1];

    if ( command == "bake" && argc >= 4 )
        return Bake( argv[2], argv[3] );

    if ( command == "info" )
        return Info( argv[2] );

    if ( command == "slice" && argc >= 4 )
    {
        SlicePlane  plane{ 0, 2, 1 };
        int         index         = -1;
        std::string channel       = "profile";
        int         magnification = 4;

        for ( int i = 4; i + 1 < argc; i += 2 )
        {
            const std::string option = argv[i];
            const std::string value  = argv[i + 1];

            if ( option == "--plane" )
            {
                if ( !ParsePlane( value, plane ) )
                {
                    std::fprintf( stderr, "CloudVolumeBaker: unknown plane '%s'\n", value.c_str() );
                    return 2;
                }
            }
            else if ( option == "--index" )
                index = std::atoi( value.c_str() );
            else if ( option == "--channel" )
                channel = value;
            else if ( option == "--scale" )
                magnification = std::atoi( value.c_str() );
            else
                return Usage();
        }

        return Slice( argv[2], argv[3], plane, index, channel, magnification );
    }

    return Usage();
}
