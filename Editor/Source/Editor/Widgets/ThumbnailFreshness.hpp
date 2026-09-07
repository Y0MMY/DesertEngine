#pragma once

#include <chrono>
#include <filesystem>

namespace Desert::Editor::ThumbnailFreshness
{
    /**
     * @brief Is the cached PNG the picture to SHOW, or the picture to REPLACE?
     *
     * THE RELATION THIS EXISTS TO KEEP. Two sides of the thumbnail system ask that question, and until
     * this header they asked it differently:
     *
     *   - the READERS (the asset browser's grid, its preview pane, the Details material slot) called a
     *     thumbnail usable only when its PNG existed AND the asset was not meaningfully newer than it;
     *   - the WRITER (ThumbnailService::ShouldQueue) called a thumbnail done when its PNG merely EXISTED.
     *
     * Individually correct, and together they leave a hole. An asset whose source is newer than its PNG
     * is judged unusable by every reader and already-cached by the queue, so nothing is drawn and nothing
     * is scheduled — permanently, and across sessions, because neither side ever changes the two facts it
     * disagrees about. The asset shows a flat colour swatch for the rest of the project's life. That is
     * not a hypothetical: `git checkout`, a branch switch, a Save from anything other than the Material
     * Editor (which deletes the PNG and so escapes the trap by accident) all move a `.demat`'s modtime
     * past its thumbnail's.
     *
     * So the rule is written ONCE and both sides ask it. The property that matters is not the margin or
     * the comparison — it is that the two verdicts are exhaustive: every observable state is either shown
     * or scheduled, and none is both nothing. Desert/Tests/Editor/ThumbnailFreshness asserts exactly that,
     * over every combination, rather than re-checking the arithmetic.
     *
     * Device-free and header-only on purpose, for the reason ThumbnailKey.hpp gives next door: the
     * decision then belongs to a test instead of to a launched editor.
     */

    /**
     * @brief How much newer the source must be before its thumbnail is called stale.
     *
     * Coarse-resolution filesystems (FAT/exFAT round to 2 s, some network mounts worse) report a source
     * as slightly newer than a PNG written moments after it. Without a margin that reads as "stale" on
     * the very frame the capture landed, and the thumbnail regenerates forever.
     */
    constexpr std::chrono::seconds kSourceNewerMargin{ 3 };

    /// What the filesystem said about one asset and its cached picture. A struct rather than two loose
    /// parameters so the judgement below cannot be called with the two stamps the wrong way round.
    struct Observation
    {
        bool PngExists = false;

        /// Both modtimes were readable. When they are not, there is no evidence of staleness — see Judge.
        bool StampsReadable = false;

        /// source modtime minus PNG modtime. Negative (the normal case) means the PNG is the younger file.
        std::chrono::seconds SourceNewerBy{ 0 };
    };

    enum class Verdict
    {
        Show,   ///< the PNG on disk is the picture; decode and draw it, queue nothing
        Capture ///< there is no usable PNG; draw a placeholder and let the service render one
    };

    /**
     * @brief The one answer. Total by construction: there is no third outcome and no state without one.
     *
     * An unreadable pair of stamps yields Show rather than Capture, and that direction is deliberate: a
     * PNG that exists is evidence, a modtime that could not be read is not, and re-rendering on the
     * absence of evidence would make an unreadable clock into an endless capture loop.
     */
    [[nodiscard]] constexpr Verdict Judge( const Observation& seen ) noexcept
    {
        if ( !seen.PngExists )
            return Verdict::Capture;
        if ( !seen.StampsReadable )
            return Verdict::Show;
        return seen.SourceNewerBy > kSourceNewerMargin ? Verdict::Capture : Verdict::Show;
    }

    /**
     * @brief Ask the filesystem the two questions Judge needs.
     *
     * Separate from Judge so the DECISION stays pure and testable while the syscalls stay in one place
     * instead of the three copies that used to spell this out (FileExplorer's material grid, its mesh
     * grid, and the Details material slot — each with its own `std::error_code` reuse bug, where the
     * second last_write_time call could clear the failure the first one reported).
     */
    [[nodiscard]] inline Observation Observe( const std::filesystem::path& png,
                                              const std::filesystem::path& source )
    {
        Observation seen;

        std::error_code existsEc;
        seen.PngExists = std::filesystem::exists( png, existsEc ) && !existsEc;
        if ( !seen.PngExists )
            return seen;

        // ONE error code per call. The three sites this replaces shared one across both stats, so a
        // failure on the first was erased by success on the second and the comparison then ran on a
        // default-constructed time point.
        std::error_code pngEc;
        std::error_code srcEc;
        const auto      pngTime = std::filesystem::last_write_time( png, pngEc );
        const auto      srcTime = std::filesystem::last_write_time( source, srcEc );
        if ( pngEc || srcEc )
            return seen; // StampsReadable stays false: no evidence either way

        seen.StampsReadable = true;
        seen.SourceNewerBy  = std::chrono::duration_cast<std::chrono::seconds>( srcTime - pngTime );
        return seen;
    }
} // namespace Desert::Editor::ThumbnailFreshness
