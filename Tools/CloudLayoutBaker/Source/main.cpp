// CloudLayoutBaker — turns a picture into a `.dclayout`, the painted sky of Docs/Clouds §PT.
//
//   CloudLayoutBaker --out <path.dclayout> [--image <path.png>] [--figure <name>]
//                    [--size <n>] [--channels r,g,b,a] [--mask]
//
// WHY THE FIGURES ARE BUILT IN. The acceptance criterion of this phase is a frame in which the sky follows
// a shape somebody drew, and a criterion whose input is an image file in a repository is a criterion that
// cannot be re-run when the generator changes — the picture would have to be found, and nobody would know
// whether the one they found was the one that was measured. Two shapes are therefore GENERATED from a
// formula: `stripe`, whose boundary is a straight line a ground-level camera can see, and `letter-d`,
// which is unmistakable from above and cannot be confused with anything a hash produces.
//
//   * `stripe`   the western half of the world painted, the eastern half clear. Visible from the GROUND,
//                which is where the game is played and where a curved sky makes a shape hard to read.
//   * `letter-d` a stem and a bowl. Legible only from above the layer, which is what the top-down shot of
//                the protocol is for, and the strongest possible answer to "did the sky follow the
//                painting" — a hash does not produce a letter.
//
// `--image` is the path an artist takes TODAY, and it is the only one: the PNG is read to RGBA8 and handed
// to Assets::MakeCloudLayoutFromImage. THERE IS NO AUTHORING WINDOW IN THIS PHASE — the Details slot takes
// a finished `.dclayout` and this is what makes one — and when a window arrives it has to call that same
// function rather than grow a second reading of what a picture means.
//
// GPU-FREE, ASSET-LAYER-FREE, filesystem only through <fstream>. See the premake file for why that is
// checked rather than hoped for.

#include <Engine/Assets/CloudLayout.hpp>

#include <stb_image/stb_image.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    int Usage()
    {
        std::fprintf( stderr,
                      "usage: CloudLayoutBaker --out <path.dclayout> [--image <path.png>] [--figure <name>]\n"
                      "                        [--size <n>] [--channels r,g,b,a] [--mask]\n"
                      "  --out       where to write the layout\n"
                      "  --image     a square PNG/JPG/TGA to paint with; RGBA8 after decode\n"
                      "  --figure    generate instead of reading: 'stripe' or 'letter-d'\n"
                      "  --size      side of a generated figure in texels, %u..%u (default 512)\n"
                      "  --channels  which SOURCE channel feeds each of the four species slots,\n"
                      "              e.g. 0,0,0,0 to put one greyscale painting on every slot (default 0,1,2,3)\n"
                      "  --mask      take the source's alpha as the add/remove mask; without it the layout\n"
                      "              carries no mask at all, because an opaque image's alpha is 255 everywhere\n"
                      "              and that would silently add cloud to the whole sky\n",
                      Desert::Assets::kCloudLayoutMinResolution, Desert::Assets::kCloudLayoutMaxResolution );
        return 2;
    }

    /// The western half painted on every channel, the eastern half clear, with a soft edge one texel wide
    /// so the boundary is a line rather than a staircase of the table's own resolution.
    ///
    /// THE MEAN IS EXACTLY A HALF BY CONSTRUCTION, which matters: the pattern is applied about its own
    /// mean, so a figure whose mean is a half moves the sky's cover by nothing at all while moving every
    /// individual cloud. That makes it the fixture that separates "the painting is applied" from "the
    /// painting made the sky thicker".
    std::vector<unsigned char> Stripe( uint32_t side, bool maskFollowsFigure )
    {
        std::vector<unsigned char> pixels( static_cast<size_t>( side ) * side * 4u, 0u );

        for ( uint32_t y = 0; y < side; ++y )
            for ( uint32_t x = 0; x < side; ++x )
            {
                const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( side );

                // Two edges, not one: the table wraps, so a single step would put a second, unintended
                // boundary at u = 0 where the last column meets the first. Both are placed deliberately.
                const float edge = 1.0f / static_cast<float>( side );
                const float lit  = ( u > edge && u < 0.5f - edge )                 ? 1.0f
                                   : ( u <= edge || u >= 0.5f - edge ) && u < 0.5f ? 0.5f
                                                                                   : 0.0f;

                const unsigned char v  = static_cast<unsigned char>( std::lround( lit * 255.0f ) );
                const size_t        at = ( static_cast<size_t>( y ) * side + x ) * 4u;

                pixels[at + 0] = v;
                pixels[at + 1] = v;
                pixels[at + 2] = v;
                pixels[at + 3] = maskFollowsFigure ? v : 128u;
            }

        return pixels;
    }

    /// A capital D: a rectangular stem and half an annulus. Analytic rather than a font, because a font is
    /// a dependency and a licence for one glyph, and because a formula regenerates at any resolution.
    ///
    /// THE PROPORTIONS ARE CHOSEN TO SURVIVE THE SKY, not to look good in a texture viewer. The stroke is a
    /// TENTH of the world period wide — at the shipped 48 km region that is 4.8 km, which is more than one
    /// placement cell across (3.0 km), so the letter can be built out of whole clouds rather than out of
    /// fragments the lattice cannot express.
    std::vector<unsigned char> LetterD( uint32_t side, bool maskFollowsFigure )
    {
        std::vector<unsigned char> pixels( static_cast<size_t>( side ) * side * 4u, 0u );

        for ( uint32_t y = 0; y < side; ++y )
            for ( uint32_t x = 0; x < side; ++x )
            {
                const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( side );
                const float v = ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( side );

                const bool stem = u > 0.22f && u < 0.32f && v > 0.18f && v < 0.82f;

                const float dx   = u - 0.32f;
                const float dy   = ( v - 0.50f ) * 0.78f; // squashed, so the bowl is taller than it is wide
                const float r    = std::sqrt( dx * dx + dy * dy );
                const bool  bowl = u >= 0.32f && r > 0.20f && r < 0.30f;

                // The bar that closes the top and bottom of the bowl onto the stem.
                const bool bar =
                     u >= 0.22f && u < 0.42f && ( ( v > 0.18f && v < 0.28f ) || ( v > 0.72f && v < 0.82f ) );

                const unsigned char lit = ( stem || bowl || bar ) ? 255u : 0u;
                const size_t        at  = ( static_cast<size_t>( y ) * side + x ) * 4u;

                pixels[at + 0] = lit;
                pixels[at + 1] = lit;
                pixels[at + 2] = lit;
                pixels[at + 3] = maskFollowsFigure ? lit : 128u;
            }

        return pixels;
    }

    bool ParseChannels( const char* text, uint32_t out[Desert::Assets::kCloudLayoutChannels] )
    {
        int a = 0, b = 0, c = 0, d = 0;
        if ( std::sscanf( text, "%d,%d,%d,%d", &a, &b, &c, &d ) != 4 )
            return false;

        const int values[] = { a, b, c, d };
        for ( uint32_t i = 0; i < Desert::Assets::kCloudLayoutChannels; ++i )
        {
            if ( values[i] < 0 || values[i] > 3 )
                return false;
            out[i] = static_cast<uint32_t>( values[i] );
        }
        return true;
    }
} // namespace

int main( int argc, char** argv )
{
    std::string out;
    std::string image;
    std::string figure;
    uint32_t    side     = 512u;
    bool        takeMask = false;

    uint32_t channels[Desert::Assets::kCloudLayoutChannels] = { 0u, 1u, 2u, 3u };

    for ( int i = 1; i < argc; ++i )
    {
        const auto next = [&]( const char* what ) -> const char*
        {
            if ( i + 1 >= argc )
            {
                std::fprintf( stderr, "%s needs a value\n", what );
                return nullptr;
            }
            return argv[++i];
        };

        if ( std::strcmp( argv[i], "--out" ) == 0 )
        {
            const char* value = next( "--out" );
            if ( !value )
                return Usage();
            out = value;
        }
        else if ( std::strcmp( argv[i], "--image" ) == 0 )
        {
            const char* value = next( "--image" );
            if ( !value )
                return Usage();
            image = value;
        }
        else if ( std::strcmp( argv[i], "--figure" ) == 0 )
        {
            const char* value = next( "--figure" );
            if ( !value )
                return Usage();
            figure = value;
        }
        else if ( std::strcmp( argv[i], "--size" ) == 0 )
        {
            const char* value = next( "--size" );
            if ( !value )
                return Usage();
            side = static_cast<uint32_t>( std::atoi( value ) );
        }
        else if ( std::strcmp( argv[i], "--channels" ) == 0 )
        {
            const char* value = next( "--channels" );
            if ( !value || !ParseChannels( value, channels ) )
            {
                std::fprintf( stderr, "--channels wants four numbers in 0..3, e.g. 0,0,0,0\n" );
                return Usage();
            }
        }
        else if ( std::strcmp( argv[i], "--mask" ) == 0 )
        {
            takeMask = true;
        }
        else
        {
            std::fprintf( stderr, "unknown argument '%s'\n", argv[i] );
            return Usage();
        }
    }

    if ( out.empty() )
        return Usage();

    // EXACTLY ONE SOURCE, and saying so is cheaper than deciding which wins. A tool that quietly preferred
    // one over the other would write a file whose provenance is a guess about argument order.
    if ( image.empty() == figure.empty() )
    {
        std::fprintf( stderr, "give exactly one of --image and --figure\n" );
        return Usage();
    }

    std::vector<unsigned char> pixels;
    uint32_t                   width  = side;
    uint32_t                   height = side;

    if ( !figure.empty() )
    {
        if ( side < Desert::Assets::kCloudLayoutMinResolution || side > Desert::Assets::kCloudLayoutMaxResolution )
        {
            std::fprintf( stderr, "--size %u lies outside [%u, %u]\n", side,
                          Desert::Assets::kCloudLayoutMinResolution, Desert::Assets::kCloudLayoutMaxResolution );
            return 1;
        }

        if ( figure == "stripe" )
            pixels = Stripe( side, takeMask );
        else if ( figure == "letter-d" )
            pixels = LetterD( side, takeMask );
        else
        {
            std::fprintf( stderr, "unknown figure '%s'; known: stripe, letter-d\n", figure.c_str() );
            return 1;
        }
    }
    else
    {
        int      w = 0, h = 0, comp = 0;
        stbi_uc* decoded = stbi_load( image.c_str(), &w, &h, &comp, 4 );
        if ( !decoded )
        {
            std::fprintf( stderr, "'%s' could not be read as an image: %s\n", image.c_str(),
                          stbi_failure_reason() ? stbi_failure_reason() : "unknown" );
            return 1;
        }

        width  = static_cast<uint32_t>( w );
        height = static_cast<uint32_t>( h );
        pixels.assign( decoded, decoded + static_cast<size_t>( w ) * h * 4 );
        stbi_image_free( decoded );

        std::printf( "read '%s': %dx%d, %d source channels\n", image.c_str(), w, h, comp );
    }

    auto made = Desert::Assets::MakeCloudLayoutFromImage( pixels, width, height, channels, takeMask );
    if ( !made )
    {
        std::fprintf( stderr, "%s\n", made.GetError().c_str() );
        return 1;
    }

    const Desert::Assets::CloudLayoutData& layout = made.GetValue();

    auto encoded = Desert::Assets::EncodeCloudLayout( layout );
    if ( !encoded )
    {
        std::fprintf( stderr, "%s\n", encoded.GetError().c_str() );
        return 1;
    }

    const std::vector<unsigned char>& bytes = encoded.GetValue();

    std::ofstream file( out, std::ios::binary | std::ios::trunc );
    if ( !file )
    {
        std::fprintf( stderr, "'%s' could not be opened for writing\n", out.c_str() );
        return 1;
    }

    file.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
    if ( !file )
    {
        std::fprintf( stderr, "'%s' was opened but the %zu bytes could not be written\n", out.c_str(),
                      bytes.size() );
        return 1;
    }

    // The means are printed because they are the number that keeps the Coverage slider honest, and a
    // painting whose mean is near 0 or near 1 has very little room to redistribute anything — which is a
    // thing worth knowing when the sky does less than expected.
    std::printf( "wrote '%s': %ux%u, pattern %s, mask %s, %zu bytes, content %08x\n"
                 "  channel means: %.4f %.4f %.4f %.4f\n",
                 out.c_str(), layout.Resolution, layout.Resolution, layout.HasPattern() ? "yes" : "no",
                 layout.HasMask() ? "yes" : "no", bytes.size(), layout.ContentHash, layout.PatternMean[0],
                 layout.PatternMean[1], layout.PatternMean[2], layout.PatternMean[3] );

    return 0;
}
