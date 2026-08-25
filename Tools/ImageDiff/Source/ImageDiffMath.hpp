#pragma once

// The arithmetic of ImageDiff, separated from its I/O for exactly the reason LatticePeakMath.hpp is:
// an instrument nobody can break on purpose is an instrument nobody can trust. Every number the tool
// prints is produced here, by functions that touch no file and hold no state, so the suite in
// Desert/Tests/Engine/ImageDiffMath can feed them fields it constructed and check the answers against
// arithmetic done by hand.
//
// Header-only and dependency-free on purpose: the CLI vendors stb_image, and the test must not.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Desert::Tools::ImageDiff
{
    // Everything the tool reports about one pair of rectangles.
    //
    // `Coherence` is the quantity this instrument exists for and the one no existing tool can produce;
    // see CoherenceOf below for what it means and why a scalar difference is not enough.
    struct DiffResult
    {
        std::size_t Pixels       = 0; // pixels in the rectangle
        std::size_t Differing    = 0; // pixels where at least one channel differs
        int         MaxAbs       = 0; // largest per-channel |a - b|, 0..255
        int         MaxX         = 0; // where that maximum sits
        int         MaxY         = 0;
        double      MeanAbs      = 0.0; // mean per-channel |a - b|, 0..255
        double      RmsAbs       = 0.0; // root mean square of the same, 0..255
        double      LumaBias     = 0.0; // mean (Yb - Ya), SIGNED, 0..255
        double      MeanAbsLuma  = 0.0; // mean |Yb - Ya|, 0..255
        double      MeanGradLuma = 0.0; // mean |first difference of the luma error field|, 0..255
        double      Coherence    = 0.0; // MeanAbsLuma / MeanGradLuma, see below
    };

    // Rec.709 luma of an 8-bit triple, kept in 0..255 rather than normalised so that every figure the
    // tool prints is in the same unit an author reads off a pixel probe. ImageStat normalises because
    // it reports percentiles of a distribution; here the numbers are DIFFERENCES, and "3 of 255" is the
    // sentence that gets written in the calibration document.
    inline double Luma255( int r, int g, int b )
    {
        return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    }

    // HOW SMOOTH THE ERROR IS, which is the axis that separates the two failures a temporal
    // reconstruction has.
    //
    // A history that is accepted when it should have been rejected produces a GHOST: a displaced copy of
    // the image, so the error field is large and SMOOTH. A history that is rejected when it should have
    // been kept produces SPECKLE: the pixel falls back on a single quarter-resolution sample, so the
    // error field is the same size but changes sign between neighbours. Both move MeanAbs by the same
    // amount and the difference between them decides which mechanism, if any, is worth adding — so a
    // scalar difference cannot answer the question this programme is asking.
    //
    // The ratio of the error to its own first difference does answer it. For a field of independent
    // samples the mean absolute first difference is larger than the mean absolute value itself, so the
    // ratio sits below one; for a field that varies slowly the first difference collapses and the ratio
    // grows without bound. The absolute value is not calibrated against anything — it is a
    // ratio between two frames' worth of the same measurement that carries the meaning.
    //
    // A field that is exactly zero everywhere has no shape to describe, and 0/0 is not a coherence of
    // any value: the caller gets 0.0 and is expected to read Differing == 0 next to it.
    inline double CoherenceOf( double meanAbsLuma, double meanGradLuma )
    {
        if ( meanGradLuma <= 1e-12 )
            return 0.0;
        return meanAbsLuma / meanGradLuma;
    }

    // The comparison itself. @p a and @p b are tightly packed 8-bit images of the same size with
    // @p channels components each (>= 3; a fourth is compared like the others but does not enter the
    // luma figures), and the rectangle is half-open: [x0, x1) x [y0, y1).
    //
    // The caller is responsible for clamping the rectangle into both images. An out-of-range rectangle
    // is refused rather than clamped here, because a rectangle that silently shrank would make two runs
    // of the tool comparable only by accident — and every number in this programme's calibration
    // document is a comparison between two runs.
    inline bool Compare( const std::uint8_t* a, const std::uint8_t* b, int width, int height, int channels, int x0,
                         int y0, int x1, int y1, DiffResult& out )
    {
        if ( !a || !b || channels < 3 )
            return false;
        if ( x0 < 0 || y0 < 0 || x1 > width || y1 > height || x1 - x0 < 2 || y1 - y0 < 2 )
            return false;

        const int rectW = x1 - x0;
        const int rectH = y1 - y0;

        // The luma error field is materialised because the coherence needs its first differences, and a
        // first difference cannot be computed in a single pass over the pixels without keeping the
        // previous row anyway. One row would be enough; the whole field costs a few megabytes at the
        // sizes this tool sees and makes the two loops below independently readable.
        std::vector<double> lumaError( static_cast<std::size_t>( rectW ) * rectH, 0.0 );

        DiffResult r;
        r.Pixels = static_cast<std::size_t>( rectW ) * rectH;

        double sumAbs     = 0.0;
        double sumSquares = 0.0;
        double sumLuma    = 0.0;
        double sumAbsLuma = 0.0;

        for ( int y = y0; y < y1; ++y )
        {
            for ( int x = x0; x < x1; ++x )
            {
                const std::size_t   index = static_cast<std::size_t>( y ) * width + x;
                const std::uint8_t* pa    = a + index * channels;
                const std::uint8_t* pb    = b + index * channels;

                bool differs = false;
                for ( int c = 0; c < channels; ++c )
                {
                    const int delta = static_cast<int>( pb[c] ) - static_cast<int>( pa[c] );
                    const int abs   = delta < 0 ? -delta : delta;
                    if ( abs != 0 )
                        differs = true;
                    sumAbs += abs;
                    sumSquares += static_cast<double>( abs ) * abs;
                    if ( abs > r.MaxAbs )
                    {
                        r.MaxAbs = abs;
                        r.MaxX   = x;
                        r.MaxY   = y;
                    }
                }
                if ( differs )
                    ++r.Differing;

                const double lumaDelta = Luma255( pb[0], pb[1], pb[2] ) - Luma255( pa[0], pa[1], pa[2] );
                lumaError[static_cast<std::size_t>( y - y0 ) * rectW + ( x - x0 )] = lumaDelta;
                sumLuma += lumaDelta;
                sumAbsLuma += lumaDelta < 0 ? -lumaDelta : lumaDelta;
            }
        }

        const double samples = static_cast<double>( r.Pixels ) * channels;
        r.MeanAbs            = sumAbs / samples;
        r.RmsAbs             = std::sqrt( sumSquares / samples );
        r.LumaBias           = sumLuma / static_cast<double>( r.Pixels );
        r.MeanAbsLuma        = sumAbsLuma / static_cast<double>( r.Pixels );

        // BOTH AXES, for the same reason LineJump reports both: an error that is smooth along rows and
        // ragged along columns is a real and different thing from one that is smooth in both, and
        // averaging the two first differences together is the cheapest way to stop being able to tell.
        // They are summed into one mean here because the coherence is a single ratio; the two counts
        // differ by one row and one column, so each is normalised by its own count before the average.
        double      sumGradX = 0.0;
        double      sumGradY = 0.0;
        std::size_t countX   = 0;
        std::size_t countY   = 0;
        for ( int y = 0; y < rectH; ++y )
            for ( int x = 1; x < rectW; ++x )
            {
                const std::size_t i = static_cast<std::size_t>( y ) * rectW + x;
                sumGradX += std::fabs( lumaError[i] - lumaError[i - 1] );
                ++countX;
            }
        for ( int y = 1; y < rectH; ++y )
            for ( int x = 0; x < rectW; ++x )
            {
                const std::size_t i = static_cast<std::size_t>( y ) * rectW + x;
                sumGradY += std::fabs( lumaError[i] - lumaError[i - rectW] );
                ++countY;
            }

        const double gradX = countX ? sumGradX / static_cast<double>( countX ) : 0.0;
        const double gradY = countY ? sumGradY / static_cast<double>( countY ) : 0.0;
        r.MeanGradLuma     = 0.5 * ( gradX + gradY );
        r.Coherence        = CoherenceOf( r.MeanAbsLuma, r.MeanGradLuma );

        out = r;
        return true;
    }
} // namespace Desert::Tools::ImageDiff
