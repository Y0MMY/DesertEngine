#pragma once

#include <Common/Core/ResultStr.hpp>
#include <Engine/Assets/CloudNoiseVolume.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Desert::Assets
{
    /**
     * @file
     * @brief The TILED SLICE SHEET: a cloud noise volume laid out flat, so it can leave this engine and
     *        come back.
     *
     * WHY A SHEET AND NOT A NEW BINARY FORMAT. `.dcnv` already is our binary format, and a second one would
     * only move the problem: an artist cannot open either in the tool they own. The sheet is a plain RGBA8
     * image — the volume's Z slices tiled onto one flat picture — so every painting, compositing and
     * houdini-shaped tool in existence can read and write it with no plugin and no library on our side.
     *
     * THE CONVENTION IS NOT INVENTED HERE, and it was checked against the source rather than against
     * recollection:
     *
     *   UNREAL builds a Volume Texture from exactly this. `UVolumeTexture::UpdateSourceFromSourceTexture`
     *   walks slices as `RefTile2DPosX = (PosZ % Num2DTileX) * TileSizeX` and
     *   `RefTile2DPosY = (PosZ / Num2DTileX) * TileSizeY` — row-major, x fastest, slice 0 at the first
     *   stored tile. Its shader-side `PseudoVolumeTexture` path in `Common.ush` agrees. That is the
     *   ordering implemented below, byte for byte.
     *
     *   HOUDINI writes it. The Labs "Volume Texture Export" SOP exports a density field as tiled slices and
     *   its own documentation says the presets exist to "match UE4's expected volume texture inputs".
     *
     *   UNITY reads it (Texture Shape 3D, Columns and Rows) — though Unity does not document its cell
     *   ordering anywhere, so that one is an interoperability hope rather than a checked fact.
     *
     * NUBIS CUBED IS **NOT** A PRECEDENT FOR THE TILING, and the mistake is worth recording because this
     * file's neighbour makes it look like one. Guerrilla shipped their 128^3 four-channel noise as a
     * NUMBERED SEQUENCE OF PER-SLICE TGAs plus a VDB — not as a tiled atlas. Cite them for the size and the
     * channel meanings, which is what `CloudNoiseVolume.hpp` correctly does, and cite Unreal and Houdini
     * for the layout.
     *
     * THE Y AXIS RUNS DOWNWARD, because PNG says so: the specification (§4.2) stores scanlines "from top to
     * bottom", and Unreal's tiles are indexed in that same stored order. Worth stating because TGA defaults
     * to the OPPOSITE (origin bottom-left), so a sheet exported as TGA by another tool is vertically
     * mirrored against this one. Nothing here reads TGA; if that ever changes, this is the line that has to
     * be honoured rather than discovered.
     *
     * WHAT ROUND-TRIPS AND WHAT DOES NOT, stated here because it is the whole contract of this file:
     *
     *   THE VOXELS ROUND-TRIP EXACTLY. RGBA8 in, RGBA8 out, no resampling, no colour management, no
     *   premultiplication. `.dcnv` -> sheet -> `.dcnv` reproduces every one of the 4*N^3 payload bytes,
     *   and the suite asserts it rather than hoping for it.
     *
     *   THE RECIPE DOES NOT, AND CANNOT. A sheet is pixels; it has nowhere to keep a seed, four lattice
     *   periods and a curl strength. So a volume that came back from a sheet is NOT the volume that left —
     *   it is the same voxels with no recipe behind them, and that is exactly what
     *   @ref CloudNoiseVolumeOrigin::Imported records. Fabricating a plausible recipe for it would be the
     *   "two statements of one fact that disagree" defect this programme keeps paying for: pressing Bake
     *   would then quietly replace the artist's voxels with different ones the header claimed to describe.
     *
     * WHY NOT PNG TEXT CHUNKS FOR THE RECIPE. They would survive our own writer and be dropped by the first
     * tool the artist opened the sheet in, which is a round trip that works until it silently does not.
     * Losing the recipe LOUDLY is better than keeping it unreliably.
     */

    /// Where every slice of a volume sits on the flat sheet. Computed, never typed: the importer derives it
    /// from the image it was handed and compares, so a sheet that does not lay out is refused with numbers
    /// instead of being cropped or stretched into one that does.
    struct CloudNoiseSheetLayout
    {
        uint32_t Resolution = 0u; ///< voxels per axis, N — also the number of slices and each tile's side
        uint32_t TilesX     = 0u; ///< slices across the sheet
        uint32_t TilesY     = 0u; ///< slices down the sheet; TilesX * TilesY == Resolution exactly
        uint32_t Width      = 0u; ///< TilesX * Resolution
        uint32_t Height     = 0u; ///< TilesY * Resolution
    };

    /**
     * @brief The one layout a volume of @p resolution is written to, and the only one it is read back from.
     *
     * THE TILING IS DERIVED, SO THERE IS NOTHING TO AGREE ABOUT. TilesX is the smallest power of two that
     * is at least sqrt(N), and TilesY is whatever is left. Both are then powers of two because N is, so the
     * SHEET is power-of-two on both axes — which matters because a sheet is an image, and an artist's tool
     * will eventually want to make it a texture.
     *
     *   N = 64  -> 8 x 8 tiles  ->  512 x 512
     *   N = 128 -> 16 x 8 tiles -> 2048 x 1024
     *
     * THE 128 SHEET IS NON-SQUARE AND CANNOT BE OTHERWISE, and that has a consequence for anyone taking one
     * of our sheets INTO Unreal that is worth knowing before it costs an afternoon. A cube of N^3 needs
     * `cols * rows == N`, so N = 128 admits 16x8, 8x16 or 32x4 and never a square (a square would be
     * 1448 a side, which is not a whole number of tiles). Unreal's "Create Volume Texture" guesses the tile
     * size as `cbrt(pixels)` and then `round(sqrt(...))`, which is only correct for a SQUARE sheet: fed our
     * 2048x1024 it derives 186x93 tiles and a depth of 121, which is garbage. The artist must type Tile Size
     * X and Y = 128 by hand. The 64 sheet is square and imports correctly on its own.
     *
     * A resolution the container would not accept is refused here too, by calling the container's own
     * check — the sheet cannot be a second opinion about what size a volume may be.
     */
    Common::ResultStr<CloudNoiseSheetLayout> CloudNoiseSheetLayoutFor( uint32_t resolution );

    /// The sheet's pixels and the shape they are in. RGBA8, `4 * Width * Height`, x fastest — the layout
    /// `stbi_write_png` takes and `stbi_load(..., 4)` returns, so neither end needs a conversion pass.
    struct CloudNoiseSheetImage
    {
        CloudNoiseSheetLayout      Layout;
        std::vector<unsigned char> Pixels;
    };

    /**
     * @brief Lays a volume out flat.
     *
     * SLICE z GOES TO TILE (z % TilesX, z / TilesX): left to right, then top to bottom, slice 0 at the
     * top-left. Within a tile, voxel (x, y) is texel (x, y) with y increasing DOWNWARD, which is the order
     * an image file stores rows in and the order the panel's own slice preview already draws them in — so
     * what an artist sees in the editor and what they open in their tool are the same picture.
     *
     * Total on any volume the container accepts; the only failure is a volume whose payload does not match
     * its own resolution, which is a corrupt @p volume rather than a bad request.
     */
    Common::ResultStr<CloudNoiseSheetImage> EncodeCloudNoiseVolumeToSheet( const CloudNoiseVolumeData& volume );

    /**
     * @brief Reads a flat sheet back into a volume, or says why these pixels are not one.
     *
     * REFUSES RATHER THAN RESAMPLES, and this is the requirement the task was given rather than a
     * preference: a sheet that does not lay out into a legal cube is reported with BOTH the size that
     * arrived and the sizes that would have worked. Stretching 2000x1000 into 2048x1024 would be an opinion
     * about somebody's data, and a volume silently interpolated is a cloud shape nobody can trace back to
     * what they exported.
     *
     * The returned volume carries @ref CloudNoiseVolumeOrigin::Imported and an EMPTY recipe — see the file
     * comment. Its @c Resolution is real, because that one number the pixels genuinely do state.
     *
     * @param pixels RGBA8, `4 * width * height`, x fastest.
     */
    Common::ResultStr<CloudNoiseVolumeData>
    DecodeCloudNoiseVolumeFromSheet( const std::vector<unsigned char>& pixels, uint32_t width, uint32_t height );

    /// Every sheet size a volume could legally be, smallest first, as "512x512 (64^3)" — for the refusal
    /// message, so a rejected import tells the artist what to make instead of only what was wrong.
    std::string CloudNoiseSheetSizesDescription();

    /// The extension the export dialog proposes. PNG because it is lossless, 8-bit RGBA, and the one format
    /// every tool in the chain reads — a JPEG sheet would round-trip the voxels to "nearly", which for a
    /// field the cloud shape is eroded with is not a round trip at all.
    inline constexpr const char* kCloudNoiseSheetExtension = ".png";
} // namespace Desert::Assets
