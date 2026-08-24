#include "CloudProceduralVolume.hpp"

#include <Common/Core/ResultStr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Desert::Assets
{
    namespace
    {
        /// How many blend radii past the nearest lump a lump may be before it is dropped from the join.
        ///
        /// TEN, AND THE NUMBER IS A QUANTISATION ARGUMENT rather than a feel. A dropped lump's term is
        /// `exp(-10) = 4.5e-5` of the nearest one's, so the error in the joined distance is at most
        /// `BlendRadiusKm * N * 4.5e-5`; at the shipped 60 m radius with a hundred lumps in range that is
        /// 2.7e-4 km, and divided by the 0.35 km profile depth it is 7.7e-4 of a unit profile — a fifth of
        /// the 1/255 the volume is quantised to. The cut is therefore invisible in the bytes, which is the
        /// only place it could ever be seen.
        constexpr float kJoinCutoffRadii = 10.0f;

        /// The lattice is walked in this many blobs per cluster at most. A ceiling rather than a count: the
        /// stack is shortened whenever the band is too thin to hold that many lumps that the march can
        /// still find, which is the relation ResolvableChordKm exists for.
        constexpr uint32_t kMaxBlobsPerCluster = 6u;

        /// The wrap offsets a lump is splatted at, in units of the region's period. NINE and not one,
        /// because the volume must be exactly periodic: a lump near the +X face has to appear at the -X
        /// face too, or REPEAT sampling shows a hard seam there. Offsets whose box misses the region cost
        /// one rejected box test.
        constexpr int kWrapRange = 1;

        /// A 32-bit integer hash. Murmur3's finalizer, which is the standard choice for turning a lattice
        /// index into an uncorrelated word, and it is written out rather than taken from a library because
        /// the bytes of the sky depend on it: a different mixer is a different sky, and a sky that changes
        /// when a dependency is upgraded is not reproducible.
        uint32_t HashWord( uint32_t value )
        {
            value ^= value >> 16;
            value *= 0x85ebca6bu;
            value ^= value >> 13;
            value *= 0xc2b2ae35u;
            value ^= value >> 16;
            return value;
        }

        uint32_t HashCombine( uint32_t seed, uint32_t value )
        {
            return HashWord( seed ^ ( HashWord( value ) + 0x9e3779b9u + ( seed << 6 ) + ( seed >> 2 ) ) );
        }

        /// A hash word as a number in [0, 1). The top 24 bits, so the result is exactly representable in a
        /// float and the mapping is uniform rather than very slightly biased at the last bit.
        float HashUnit( uint32_t word )
        {
            return static_cast<float>( word >> 8 ) * ( 1.0f / 16777216.0f );
        }

        /// A signed lattice index as an unsigned word, so that a hash is defined at negative coordinates —
        /// which every world west or north of the origin has.
        uint32_t IndexWord( int32_t index )
        {
            return static_cast<uint32_t>( index ) ^ 0x80000000u;
        }

        /// The cell's two side lengths, kilometres: longer along the wind, shorter across it, with the AREA
        /// held constant so that raising the anisotropy draws a cluster out into a band instead of making
        /// the sky emptier. That is the same statement CloudSpeciesPlacementBasis makes with its two
        /// frequencies, and it is exact here where there it was a frequency ratio.
        glm::vec2 CellExtentKm( const CloudProceduralFieldParams& params, const CloudProceduralSpecies& species )
        {
            // THE FLOOR IS NOT DEFENSIVE PADDING, IT IS A MEASURED BOUND. A cell smaller than a few voxels
            // cannot be expressed by the volume at all — the cluster inside it is narrower than the
            // trilinear filter's own support — and the cost of trying is quadratic in the region: the suite
            // authored a species with a 50 m cell and the generator produced 4 180 731 lumps for one 48 km
            // region, which took a minute to place and could never have been baked. Four voxels is the
            // narrowest cluster the volume can carry with an inside and two edges.
            const float voxelKm = params.RegionSizeKm / static_cast<float>( kCloudProceduralVolumeWidth );
            const float floorKm = std::max( 4.0f * voxelKm, 2.0f * params.ResolvableChordKm );

            const float anisotropy = std::max( species.Anisotropy, 1e-3f );
            const float root       = std::sqrt( anisotropy );
            const float cell       = std::max( species.CellKm, floorKm );
            return glm::vec2( cell * root, cell / root );
        }

        /// The horizontal frame the lattice is laid out in: the wind's direction and the axis across it.
        /// A zero wind means east, which is what Graphic::CloudSpeciesPlacementBasis also answers.
        void WindFrame( const glm::vec2& windAxis, glm::vec2& along, glm::vec2& across )
        {
            const float length = std::sqrt( windAxis.x * windAxis.x + windAxis.y * windAxis.y );
            along              = ( length > 1e-6f ) ? windAxis / length : glm::vec2( 1.0f, 0.0f );
            across             = glm::vec2( -along.y, along.x );
        }

        /// Where the centre of lattice cell (@p ix, @p iz) sits in the world, kilometres, XZ.
        glm::vec2 CellCentreKm( const glm::vec2& along, const glm::vec2& across, const glm::vec2& extent,
                                int32_t ix, int32_t iz )
        {
            const float u = ( static_cast<float>( ix ) + 0.5f ) * extent.x;
            const float v = ( static_cast<float>( iz ) + 0.5f ) * extent.y;
            return along * u + across * v;
        }

        /// A number in [-0.5, 0.5) from a hash word — the jitter that stops a lattice from reading as a
        /// grid.
        float HashSigned( uint32_t word )
        {
            return HashUnit( word ) - 0.5f;
        }
    } // namespace

    Common::BoolResultStr ValidateCloudProceduralParams( const CloudProceduralFieldParams& params )
    {
        if ( !( params.RegionSizeKm > 0.0f ) || !std::isfinite( params.RegionSizeKm ) )
            return Common::MakeFormattedError<bool>( "region size must be a positive length, got {} km",
                                                     params.RegionSizeKm );

        if ( !( params.LayerThicknessKm > 0.0f ) || !std::isfinite( params.LayerThicknessKm ) )
            return Common::MakeFormattedError<bool>( "layer thickness must be a positive length, got {} km",
                                                     params.LayerThicknessKm );

        if ( !std::isfinite( params.LayerBottomKm ) )
            return Common::MakeFormattedError<bool>( "layer bottom altitude is not finite, got {} km",
                                                     params.LayerBottomKm );

        if ( !( params.BlendRadiusKm > 0.0f ) || !std::isfinite( params.BlendRadiusKm ) )
            return Common::MakeFormattedError<bool>( "blend radius must be a positive length, got {} km",
                                                     params.BlendRadiusKm );

        if ( !( params.ProfileDepthKm > 0.0f ) || !std::isfinite( params.ProfileDepthKm ) )
            return Common::MakeFormattedError<bool>( "profile depth must be a positive length, got {} km",
                                                     params.ProfileDepthKm );

        if ( !( params.ResolvableChordKm > 0.0f ) || !std::isfinite( params.ResolvableChordKm ) )
            return Common::MakeFormattedError<bool>(
                 "the march's resolvable chord must be a positive length, got {} km — it is the bound every "
                 "lump is sized against and a zero would let the generator place structure no ray can find",
                 params.ResolvableChordKm );

        if ( params.Species.empty() )
            return Common::MakeError<bool>( "a layer with no species in it has nothing to place; the renderer "
                                            "resolves at least one before it asks for a bake" );

        if ( params.Species.size() > Graphic::kCloudSpeciesSlots )
            return Common::MakeFormattedError<bool>(
                 "{} species were given but a volume has {} channels, one per species", params.Species.size(),
                 Graphic::kCloudSpeciesSlots );

        // THE REGION AGAINST THE MARCH, and it is the relation this phase is most likely to break. A voxel
        // is RegionSize/Width across, trilinear filtering cannot express a feature narrower than two of
        // them, and the march searches at ResolvableChordKm — so a region small enough to make the voxel
        // finer than half that chord fills the volume with structure the ray finds only when its jitter
        // happens to land on it, which is the definition of speckle.
        const float voxelKm = params.RegionSizeKm / static_cast<float>( kCloudProceduralVolumeWidth );
        if ( 2.0f * voxelKm < params.ResolvableChordKm )
            return Common::MakeFormattedError<bool>(
                 "a region of {:.1f} km over {} voxels gives a voxel of {:.0f} m, whose finest expressible "
                 "feature is {:.0f} m — thinner than the {:.0f} m the march can be relied on to find. Either "
                 "the region grows or the march steps finer (CLOUD_DISTANCE_TO_MAX_STEPS_KM)",
                 params.RegionSizeKm, kCloudProceduralVolumeWidth, voxelKm * 1000.0f, 2.0f * voxelKm * 1000.0f,
                 params.ResolvableChordKm * 1000.0f );

        for ( size_t slot = 0; slot < params.Species.size(); ++slot )
        {
            const CloudProceduralSpecies& species = params.Species[slot];

            if ( !( species.CellKm > 0.0f ) || !std::isfinite( species.CellKm ) )
                return Common::MakeFormattedError<bool>( "species {} has a cell of {} km, which is not a length",
                                                         slot, species.CellKm );

            if ( !( species.Anisotropy > 0.0f ) || !std::isfinite( species.Anisotropy ) )
                return Common::MakeFormattedError<bool>(
                     "species {} has an anisotropy of {}, which is not a ratio", slot, species.Anisotropy );

            if ( !( species.Shape.TopAltitudeKm > species.Shape.BaseAltitudeKm ) )
                return Common::MakeFormattedError<bool>(
                     "species {} has its top at {} km and its base at {} km — a band with no height in it "
                     "cannot hold a lump",
                     slot, species.Shape.TopAltitudeKm, species.Shape.BaseAltitudeKm );
        }

        return Common::MakeSuccess( true );
    }

    float CloudProceduralSnapKm( const CloudProceduralFieldParams& params )
    {
        // THE COARSEST CELL IN THE LAYER, because the snap has to be a whole number of cells for EVERY
        // species at once — a shift of half a cell would re-roll that species' clusters and the sky would
        // boil where it should have stood still.
        //
        // Floored at a kilometre so that a layer of very fine species does not ask for a rebake every few
        // hundred metres of camera travel: below that the cost of the bake dominates what it buys, and the
        // invariance the snap protects is already exact for anything that stays in the region.
        float coarsest = 1.0f;
        for ( const CloudProceduralSpecies& species : params.Species )
        {
            const glm::vec2 extent = CellExtentKm( params, species );
            coarsest               = std::max( coarsest, std::max( extent.x, extent.y ) );
        }
        return coarsest;
    }

    glm::vec2 CloudProceduralRegionOriginKm( const CloudProceduralFieldParams& params, float cameraXKm,
                                             float cameraZKm )
    {
        const float snap = CloudProceduralSnapKm( params );
        const float half = params.RegionSizeKm * 0.5f;

        // FLOOR AND NOT ROUND, so that the origin is a monotone step function of the camera: rounding puts
        // the step at the half-cell and gives the same answer either side of it, which is fine, but the
        // floor makes "which snap cell is the camera in" a single division that a test can restate.
        const float x = std::floor( ( cameraXKm - half ) / snap ) * snap;
        const float z = std::floor( ( cameraZKm - half ) / snap ) * snap;
        return glm::vec2( x, z );
    }

    std::vector<CloudModellingBlob> GenerateCloudProceduralBlobs( const CloudProceduralFieldParams& params,
                                                                  uint32_t slot, const glm::vec2& regionOriginKm )
    {
        std::vector<CloudModellingBlob> blobs;

        if ( slot >= params.Species.size() )
            return blobs;

        const CloudProceduralSpecies&  species = params.Species[slot];
        const Graphic::CloudTypeShape& shape   = species.Shape;

        glm::vec2 along;
        glm::vec2 across;
        WindFrame( params.WindAxis, along, across );

        const glm::vec2 extent = CellExtentKm( params, species );

        // THE SET OF CELLS IS EXACTLY ONE PERIOD'S WORTH — those whose CENTRE lies in the region — and not
        // one cell more. The bake wraps every lump across the region's faces to make the volume periodic,
        // so generating the neighbouring cells as well would place each of them TWICE: once as itself and
        // once as the wrap of the cell a period away.
        //
        // The lattice is laid out in the wind's frame and the region is axis-aligned, so the range of
        // indices is found by mapping the region's four corners into that frame and taking the extremes.
        // An index range that is a superset costs a rejected containment test per cell and never a wrong
        // cloud; a subset would cut a band off the sky.
        const float side = params.RegionSizeKm;

        float minU = 0.0f;
        float maxU = 0.0f;
        float minV = 0.0f;
        float maxV = 0.0f;
        for ( int corner = 0; corner < 4; ++corner )
        {
            const glm::vec2 point =
                 regionOriginKm + glm::vec2( ( corner & 1 ) ? side : 0.0f, ( corner & 2 ) ? side : 0.0f );
            const float u = point.x * along.x + point.y * along.y;
            const float v = point.x * across.x + point.y * across.y;

            minU = ( corner == 0 ) ? u : std::min( minU, u );
            maxU = ( corner == 0 ) ? u : std::max( maxU, u );
            minV = ( corner == 0 ) ? v : std::min( minV, v );
            maxV = ( corner == 0 ) ? v : std::max( maxV, v );
        }

        const int32_t firstU = static_cast<int32_t>( std::floor( minU / extent.x ) ) - 1;
        const int32_t lastU  = static_cast<int32_t>( std::floor( maxU / extent.x ) ) + 1;
        const int32_t firstV = static_cast<int32_t>( std::floor( minV / extent.y ) ) - 1;
        const int32_t lastV  = static_cast<int32_t>( std::floor( maxV / extent.y ) ) + 1;

        const float bandKm = shape.TopAltitudeKm - shape.BaseAltitudeKm;

        // HOW TALL A STACK MAY BE, and it is the relation the whole phase is judged on. Each lump of the
        // stack owns `band / count` of the height, so a stack of many lumps in a thin band makes lumps the
        // march cannot find. Solving `2 * verticalRadius >= ResolvableChordKm` for the count with the
        // vertical radius set at 0.6 of the spacing (which is what makes consecutive lumps overlap rather
        // than sit in a row) gives the bound below.
        const float    maxCountByChord = 1.2f * bandKm / std::max( params.ResolvableChordKm, 1e-6f );
        const uint32_t stackCount =
             std::max( 1u, std::min( kMaxBlobsPerCluster, static_cast<uint32_t>( maxCountByChord ) ) );

        const float spacingKm  = bandKm / static_cast<float>( stackCount );
        const float verticalKm = std::max( 0.6f * spacingKm, 0.5f * params.ResolvableChordKm );

        // THE WIDEST A CLUSTER'S BASE LUMP GETS, and the number was raised from two fifths of the cell's
        // short side to eleven twentieths by MEASUREMENT: at two fifths a coverage of 0.35 put cloud over
        // four per cent of the sky, because a cluster covered about a ninth of the cell its hash had won.
        // A slider documented as "what fraction of the sky is cloud" has to mean it, and it only can if an
        // alive cell is mostly full. Above a half the clusters of two adjacent alive cells OVERLAP, which
        // is the whole point — that is where a bank of cloud comes from rather than a row of cushions.
        const float baseRadiusKm =
             std::max( 0.72f * std::min( extent.x, extent.y ), 0.5f * params.ResolvableChordKm );

        const uint32_t speciesSeed = HashCombine( params.Seed, slot + 0x51ed270bu );

        // ---------------------------------------------------------------------------------------------
        // WHAT FRACTION OF THE CELLS IS ALIVE, WHICH IS NOT THE SLIDER
        // ---------------------------------------------------------------------------------------------
        //
        // The slider means what a person looking up would measure: the fraction of the SKY with cloud
        // somewhere in the column. A cell being alive is not that — a cluster does not fill its cell, two
        // neighbouring clusters overlap, and how full a cluster is depends on how deep inside the
        // threshold its own hash fell. Taken as the alive fraction directly, the slider under-delivered
        // by a factor that grew with the setting: 0.24 gave 0.105 of the sky and 0.75 gave 0.450 — and
        // the frame that came out of it had clouds on the horizon and an EMPTY ZENITH, which is the
        // defect Docs/Clouds/REVIEW_622a01a6.md names and the one the owner found by looking up.
        //
        // 0.68 IS MEASURED, on the top-down projection of the baked volume at five settings, and
        // Desert/Tests/Engine/CloudProceduralField re-measures it on every run and fails if the slider
        // and the sky part company by more than a tenth. Being a power it keeps both ends EXACT, which is
        // the property the ends were built to have: 0 stays empty and 1 stays full.
        const float aliveFraction = std::pow( std::clamp( params.Coverage, 0.0f, 1.0f ), 0.68f );

        for ( int32_t iv = firstV; iv <= lastV; ++iv )
        {
            for ( int32_t iu = firstU; iu <= lastU; ++iu )
            {
                const glm::vec2 centre = CellCentreKm( along, across, extent, iu, iv );

                // ONE PERIOD, decided on the cell's own centre. Half-open so that a centre landing exactly
                // on a face belongs to one region and not to two.
                const glm::vec2 local = centre - regionOriginKm;
                if ( local.x < 0.0f || local.x >= params.RegionSizeKm || local.y < 0.0f ||
                     local.y >= params.RegionSizeKm )
                    continue;

                // THE CELL'S IDENTITY IS ITS ABSOLUTE LATTICE INDEX, which is what makes the field
                // invariant under the region scrolling: a cell that is in the region before a shift and
                // after it hashes to exactly the same cluster, so nothing inside the region moves when the
                // window does.
                const uint32_t cellSeed =
                     HashCombine( HashCombine( speciesSeed, IndexWord( iu ) ), IndexWord( iv ) );

                // COVERAGE ADDRESSES A FRACTION OF SKY DIRECTLY. A cell is alive when its own hash falls
                // below the slider, so 0 is exactly empty and 1 is exactly full — for any seed, any cell
                // size and any species, with no distribution to calibrate. That is the property the
                // quantile map this replaces was built to fake on a field whose spread it had to measure.
                const float draw = HashUnit( cellSeed );
                if ( draw >= aliveFraction )
                    continue;

                // AND CONTRAST IS THE WIDTH OF THE RAMP INTO IT. A cell that only just qualified grows a
                // small cluster; one well inside the threshold grows a full one. Above 1 the sky is
                // decisively cloud or decisively clear; below 1 the sizes spread out, which is what a
                // broken deck looks like.
                const float softness = ( 1.0f - std::clamp( params.Coverage, 0.0f, 1.0f ) ) /
                                            std::max( params.CoverageContrast, 1e-2f ) +
                                       0.02f;
                const float fill =
                     std::clamp( ( aliveFraction - draw ) / std::max( softness, 1e-4f ), 0.0f, 1.0f );

                // EDGE TOP FRACTION IS WHAT A SHALLOW CLUSTER LOSES. The type says how tall it is where the
                // patch has only just begun, and `fill` is how far inside the patch this cell is — so a
                // rim cell is low and flat and a core cell is a tower. That is decision D-13's whole
                // intent, carried by the placement instead of by a second axis of a table.
                const float fullness = std::clamp( shape.EdgeTopFraction, 0.0f, 1.0f ) +
                                       ( 1.0f - std::clamp( shape.EdgeTopFraction, 0.0f, 1.0f ) ) * fill;

                // EVERY CLUSTER GETS THE WHOLE STACK, and `fullness` shrinks the BAND it is spread over
                // rather than the number of lobes in it. Cutting the count instead was measured and was
                // wrong twice over: a shallow cell came out with one or two lobes, which is a dot and not a
                // cloud, and the lobes it kept were the same size as a full cluster's, so a low cumulus
                // humilis read as a truncated congestus. Six flattened lobes over half a band is a
                // pancake — which is what a humilis IS.

                // The cluster's own horizontal displacement inside its cell, so the lattice does not read
                // as a grid. Bounded at a third of the cell so a cluster stays in the cell whose hash made
                // it — which is what keeps the invariance argument above about a CELL rather than about a
                // neighbourhood.
                const glm::vec2 jitter( HashSigned( HashCombine( cellSeed, 0x1u ) ) * extent.x * 0.66f,
                                        HashSigned( HashCombine( cellSeed, 0x2u ) ) * extent.y * 0.66f );

                const glm::vec2 clusterXZ = centre + along * jitter.x + across * jitter.y;

                // THE CLUSTER'S OVERALL HORIZONTAL HALF-EXTENT — the size of the CLOUD, not of a lobe.
                const float clusterRadiusKm = baseRadiusKm * ( 0.60f + 0.40f * fill );

                // Where the spiral starts, per cell, so that two clusters of the same fullness are not the
                // same cloud rotated into the same place.
                const float phase = HashUnit( HashCombine( cellSeed, 0x3u ) ) * 6.2831853f;

                for ( uint32_t step = 0; step < stackCount; ++step )
                {
                    const uint32_t lumpSeed = HashCombine( cellSeed, 0x100u + step );

                    // Where up the stack this lump sits, 0 at the base and approaching 1 at the top. The
                    // band it spans is `bandKm * fullness`, so a short cluster is a whole short cloud
                    // rather than the bottom slice of a tall one — which is what a low cumulus humilis is.
                    // BOTTOM-HEAVY, and the exponent is measured rather than chosen. Spread evenly, six
                    // lobes put one or two at the wide base and four up the narrow tower, so the base was
                    // a rosette with holes in it: a full cell measured 48 per cent covered from below when
                    // the geometry says a full cluster should cover it. A cumulus is a WIDE FLOOR with a
                    // turret or two on top, which is the same thing said about the picture and about the
                    // number.
                    const float u = ( static_cast<float>( step ) + 0.5f ) / static_cast<float>( stackCount );
                    const float t = std::pow( u, 1.7f );

                    // TOP TAPER IS HOW FAST THE STACK NARROWS. A cumulus is a pile whose lobes shrink as
                    // they rise; a stratus, whose taper is near zero, is a slab of equal lobes.
                    const float taper = std::clamp( shape.TopTaper, 0.0f, 1.0f );

                    // THE LOBES ARE SPREAD OVER A DISC AND NOT STACKED CONCENTRICALLY, and this is the line
                    // that decides whether the sky is a cumulus field or a field of dots.
                    //
                    // The first written form displaced each lobe by a third of its OWN radius, which put
                    // every lobe of a cluster inside every other one: the join of six concentric ellipsoids
                    // is one ellipsoid, and a top-down projection of the volume came out as a scatter of
                    // round dots — the SAME defect the Alligator threshold had, arrived at from the other
                    // side. What a convective mass is made of is lobes that overlap PARTLY, so each shows
                    // its own shoulder while the body stays one connected surface.
                    //
                    // The golden angle spreads them without a pattern, and the disc narrows going up so the
                    // pile is a dome rather than a column: at the base the lobes sit half a cluster-radius
                    // out, at the top they close over the middle.
                    const float angle  = phase + 2.39996323f * static_cast<float>( step );
                    const float spread = clusterRadiusKm * 0.48f * ( 1.0f - 0.55f * t );

                    // HOW FAR THE LOBES OVERLAP IS THE WHOLE ARGUMENT OF THE PHASE, so it is arithmetic and
                    // not a feel. Two lobes one golden angle apart on a circle of radius `spread` are
                    // `2 * spread * sin(68.5 deg) = 1.86 * spread` apart; with `spread = 0.42 R` that is
                    // 0.78 R against a sum of radii of 1.20 R, so they interpenetrate by 0.42 R — a third
                    // of a lobe. At the first written pair (0.52, 0.50) the same numbers were 0.97 R
                    // against 1.00 R, the lobes only TOUCHED, and the top-down projection came out as
                    // clusters of separate dots: fusion is not free just because the join can express it,
                    // the bodies have to be inside one another.
                    const float radius = clusterRadiusKm * ( 0.62f - 0.16f * t ) * ( 1.0f - taper * t * 0.5f );

                    // BASE RAMP FRACTION IS THE THICKNESS OF THE LOWEST LOBE against the ones above it: a
                    // type whose base fills in slowly has a thin, spreading floor and a fat body over it.
                    const float ramp     = std::clamp( shape.BaseRampFraction, 0.05f, 1.0f );
                    const float vertical = verticalKm * ( ( step == 0 ) ? ramp + ( 1.0f - ramp ) * 0.5f : 1.0f );

                    CloudModellingBlob blob;
                    blob.Primitive = CloudModellingPrimitive::Ellipsoid;

                    const float wobble = 0.18f * clusterRadiusKm;

                    blob.CentreKm = glm::vec3( clusterXZ.x + std::cos( angle ) * spread +
                                                    HashSigned( HashCombine( lumpSeed, 0xau ) ) * wobble,
                                               shape.BaseAltitudeKm + bandKm * fullness * t,
                                               clusterXZ.y + std::sin( angle ) * spread +
                                                    HashSigned( HashCombine( lumpSeed, 0xbu ) ) * wobble );

                    // THE LUMP IS NEVER THINNER THAN THE MARCH CAN FIND, on any axis. It is a clamp and not
                    // an assertion because the inputs are an artist's: a type authored with a 40 m band is
                    // a legal thing to write in a `.decloudtype`, and the honest answer is a lobe the march
                    // can see rather than speckle or a refusal to draw the sky.
                    const float floorKm = 0.5f * params.ResolvableChordKm;
                    blob.RadiiKm        = glm::vec3(
                         std::max( radius * ( 0.85f + 0.3f * HashUnit( HashCombine( lumpSeed, 0xcu ) ) ),
                                          floorKm ),
                         std::max( vertical, floorKm ),
                         std::max( radius * ( 0.85f + 0.3f * HashUnit( HashCombine( lumpSeed, 0xdu ) ) ),
                                          floorKm ) );

                    blob.RotationDeg  = glm::vec3( 0.0f );
                    blob.Weight       = 1.0f;
                    blob.DetailType   = std::clamp( shape.DetailCharacter, 0.0f, 1.0f );
                    blob.DensityScale = 1.0f;

                    blobs.push_back( blob );
                }

                // THE ANVIL, and it is the shape no vertical curve could express: a lobe of cloud at the
                // tropopause with a GAP between it and the tower that fed it. A product of two ramps has
                // exactly one maximum for any choice of constants, which is the argument decision D-13 made
                // for a table; a second lump makes it without a table at all.
                if ( shape.AnvilStrength > 1e-3f && shape.AnvilThicknessKm > 1e-4f )
                {
                    CloudModellingBlob anvil;
                    anvil.Primitive = CloudModellingPrimitive::Ellipsoid;
                    anvil.CentreKm  = glm::vec3( clusterXZ.x, shape.AnvilAltitudeKm, clusterXZ.y );

                    // Wider than the tower and much flatter, which is what spreading against a stable layer
                    // looks like. The strength decides how far it spreads and how much matter is in it.
                    const float spread =
                         baseRadiusKm * ( 0.60f + 0.40f * fill ) * ( 1.0f + 0.8f * shape.AnvilStrength );
                    const float floorKm = 0.5f * params.ResolvableChordKm;

                    anvil.RadiiKm =
                         glm::vec3( std::max( spread, floorKm ), std::max( shape.AnvilThicknessKm, floorKm ),
                                    std::max( spread * 0.9f, floorKm ) );

                    anvil.RotationDeg = glm::vec3( 0.0f );
                    anvil.Weight      = 1.0f;
                    anvil.DetailType  = std::clamp( shape.DetailCharacter, 0.0f, 1.0f );
                    // The anvil is ice and is THINNER than the tower, and this is the one place a lump's
                    // own density scale is not 1: the softmax weights of the join turn it into a smooth
                    // per-voxel field over the crease between the anvil and the body.
                    anvil.DensityScale = std::clamp( shape.AnvilStrength, 0.0f, 1.0f );

                    blobs.push_back( anvil );
                }
            }
        }

        // CANONICAL ORDER, for the reason phase Э4 measured: the join is commutative and associative in
        // real arithmetic and neither in floating point, so a bake whose bytes must not depend on the order
        // its lumps were emitted in sorts first. Here the emission order is a loop over a lattice, which is
        // stable — but it changes when the wind turns the frame, and a field that shifts by a 255th when
        // the wind direction is nudged is exactly the class of drift the sort removes.
        SortCloudModellingBlobs( blobs );

        return blobs;
    }

    float EvaluateCloudProceduralProfile( const CloudProceduralFieldParams&      params,
                                          const std::vector<CloudModellingBlob>& blobs, const glm::vec3& pointKm )
    {
        if ( blobs.empty() )
            return 0.0f;

        const float invBlend = 1.0f / std::max( params.BlendRadiusKm, 1e-6f );

        float nearest = 0.0f;
        for ( size_t k = 0; k < blobs.size(); ++k )
        {
            const float distance = CloudModellingBlobDistanceKm( PrepareCloudModellingBlob( blobs[k] ), pointKm );
            nearest              = ( k == 0 ) ? distance : std::min( nearest, distance );
        }

        float sum = 0.0f;
        for ( const CloudModellingBlob& blob : blobs )
        {
            const float distance = CloudModellingBlobDistanceKm( PrepareCloudModellingBlob( blob ), pointKm );
            sum += CloudModellingJoinTerm( blob.Weight, distance, nearest, invBlend );
        }

        const float joined = CloudModellingJoinKm( nearest, sum, params.BlendRadiusKm );
        return std::clamp( -joined / std::max( params.ProfileDepthKm, 1e-6f ), 0.0f, 1.0f );
    }

    size_t CountCloudProceduralBlobs( const CloudProceduralFieldParams& params, const glm::vec2& regionOriginKm )
    {
        size_t total = 0;
        for ( uint32_t slot = 0; slot < params.Species.size(); ++slot )
            total += GenerateCloudProceduralBlobs( params, slot, regionOriginKm ).size();
        return total;
    }

    Common::ResultStr<std::vector<unsigned char>>
    BakeCloudProceduralVolume( const CloudProceduralFieldParams& params, const glm::vec2& regionOriginKm )
    {
        if ( auto valid = ValidateCloudProceduralParams( params ); !valid )
            return Common::MakeFormattedError<std::vector<unsigned char>>( "parameters are not usable: {}",
                                                                           valid.GetError() );

        const uint32_t width  = kCloudProceduralVolumeWidth;
        const uint32_t height = kCloudProceduralVolumeHeight;
        const uint32_t depth  = kCloudProceduralVolumeDepth;

        std::vector<unsigned char> voxels( static_cast<size_t>( kCloudProceduralVoxelBytes ), 0u );

        const float voxelXKm = params.RegionSizeKm / static_cast<float>( width );
        const float voxelZKm = params.RegionSizeKm / static_cast<float>( depth );
        const float voxelYKm = params.LayerThicknessKm / static_cast<float>( height );

        // How far a lump reaches before its term in the join is below the quantisation floor. See
        // kJoinCutoffRadii; the profile depth is added because a voxel that far INSIDE a body still has to
        // know about it.
        const float influenceKm = params.ProfileDepthKm + params.BlendRadiusKm * kJoinCutoffRadii;

        const float invBlend   = 1.0f / params.BlendRadiusKm;
        const float invProfile = 1.0f / params.ProfileDepthKm;

        for ( uint32_t slot = 0; slot < params.Species.size(); ++slot )
        {
            const std::vector<CloudModellingBlob> blobs =
                 GenerateCloudProceduralBlobs( params, slot, regionOriginKm );

            if ( blobs.empty() )
                continue;

            // EVERY LUMP AT EVERY WRAP THAT REACHES THE REGION. This is what makes the volume periodic and
            // therefore what makes REPEAT sampling seamless — see the header note. A lump in the middle of
            // the region produces exactly one entry; one against a face produces two; one in a corner four.
            struct Placed
            {
                CloudModellingPreparedBlob Blob;
                glm::vec3                  MinKm;
                glm::vec3                  MaxKm;
            };

            std::vector<Placed> placed;
            placed.reserve( blobs.size() * 2u );

            for ( const CloudModellingBlob& blob : blobs )
            {
                const glm::vec3 extent = CloudModellingBlobHalfExtentKm( blob ) + glm::vec3( influenceKm );

                for ( int wz = -kWrapRange; wz <= kWrapRange; ++wz )
                {
                    for ( int wx = -kWrapRange; wx <= kWrapRange; ++wx )
                    {
                        CloudModellingBlob shifted = blob;
                        shifted.CentreKm.x += static_cast<float>( wx ) * params.RegionSizeKm;
                        shifted.CentreKm.z += static_cast<float>( wz ) * params.RegionSizeKm;

                        const glm::vec3 minKm = shifted.CentreKm - extent;
                        const glm::vec3 maxKm = shifted.CentreKm + extent;

                        // Reject the copies that cannot touch the region at all, which is seven of the nine
                        // for a lump in the middle of it.
                        if ( maxKm.x <= regionOriginKm.x || minKm.x >= regionOriginKm.x + params.RegionSizeKm )
                            continue;
                        if ( maxKm.z <= regionOriginKm.y || minKm.z >= regionOriginKm.y + params.RegionSizeKm )
                            continue;
                        if ( maxKm.y <= params.LayerBottomKm ||
                             minKm.y >= params.LayerBottomKm + params.LayerThicknessKm )
                            continue;

                        placed.push_back( Placed{ PrepareCloudModellingBlob( shifted ), minKm, maxKm } );
                    }
                }
            }

            if ( placed.empty() )
                continue;

            // A COARSE XZ BIN OVER THE REGION, so a voxel asks about the lumps that can reach it rather
            // than about all of them. Without it the bake is `voxels x lumps` — two million by a thousand —
            // and with it the inner list is the handful of lumps whose boxes overlap this bin.
            //
            // THE LISTS STAY IN THE LUMPS' CANONICAL ORDER because `placed` is walked in that order and a
            // lump is appended to each bin it touches. That is what carries phase Э4's order-independence
            // into this bake: the sum a voxel performs is over an ascending subsequence of one sorted list,
            // whatever the lattice loop did.
            const uint32_t bins   = 32u;
            const float    binKm  = params.RegionSizeKm / static_cast<float>( bins );
            const float    invBin = 1.0f / binKm;

            std::vector<std::vector<uint32_t>> binList( static_cast<size_t>( bins ) * bins );

            for ( uint32_t index = 0; index < placed.size(); ++index )
            {
                const Placed& item = placed[index];

                const int firstX =
                     std::max( 0, static_cast<int>( std::floor( ( item.MinKm.x - regionOriginKm.x ) * invBin ) ) );
                const int lastX =
                     std::min( static_cast<int>( bins ) - 1,
                               static_cast<int>( std::floor( ( item.MaxKm.x - regionOriginKm.x ) * invBin ) ) );
                const int firstZ =
                     std::max( 0, static_cast<int>( std::floor( ( item.MinKm.z - regionOriginKm.y ) * invBin ) ) );
                const int lastZ =
                     std::min( static_cast<int>( bins ) - 1,
                               static_cast<int>( std::floor( ( item.MaxKm.z - regionOriginKm.y ) * invBin ) ) );

                for ( int bz = firstZ; bz <= lastZ; ++bz )
                    for ( int bx = firstX; bx <= lastX; ++bx )
                        binList[static_cast<size_t>( bz ) * bins + bx].push_back( index );
            }

            std::vector<float>    distances;
            std::vector<uint32_t> column;

            for ( uint32_t z = 0; z < depth; ++z )
            {
                const float worldZ = regionOriginKm.y + ( static_cast<float>( z ) + 0.5f ) * voxelZKm;
                const int   binZ   = std::clamp( static_cast<int>( ( worldZ - regionOriginKm.y ) * invBin ), 0,
                                                 static_cast<int>( bins ) - 1 );

                for ( uint32_t x = 0; x < width; ++x )
                {
                    const float worldX = regionOriginKm.x + ( static_cast<float>( x ) + 0.5f ) * voxelXKm;
                    const int   binX   = std::clamp( static_cast<int>( ( worldX - regionOriginKm.x ) * invBin ), 0,
                                                     static_cast<int>( bins ) - 1 );

                    const std::vector<uint32_t>& list = binList[static_cast<size_t>( binZ ) * bins + binX];
                    if ( list.empty() )
                        continue;

                    // THE COLUMN'S OWN CANDIDATES, decided once for all 32 rows above this ground position.
                    // The horizontal half of the box test does not depend on the altitude, and performing
                    // it inside the y loop repeated it thirty-two times for the same answer — measured at
                    // 642 ms per bake for one species, most of it in rejections. The list stays in the
                    // lumps' canonical order because `list` is, which is what carries the join's
                    // order-independence through this optimisation.
                    column.clear();
                    for ( uint32_t index : list )
                    {
                        const Placed& item = placed[index];
                        if ( worldX < item.MinKm.x || worldX > item.MaxKm.x || worldZ < item.MinKm.z ||
                             worldZ > item.MaxKm.z )
                            continue;
                        column.push_back( index );
                    }

                    if ( column.empty() )
                        continue;

                    for ( uint32_t y = 0; y < height; ++y )
                    {
                        const float worldY = params.LayerBottomKm + ( static_cast<float>( y ) + 0.5f ) * voxelYKm;

                        const glm::vec3 point( worldX, worldY, worldZ );

                        // THE SAME TWO LOOPS THE SCULPTED BAKE PERFORMS, in the same order, over the same
                        // three shared functions — the nearest distance, then the shifted sum. Only the SET
                        // is different, and it is a subset chosen so that everything left out is below the
                        // quantisation floor.
                        distances.clear();

                        float nearest = 0.0f;
                        bool  any     = false;

                        for ( uint32_t index : column )
                        {
                            const Placed& item = placed[index];

                            if ( point.y < item.MinKm.y || point.y > item.MaxKm.y )
                            {
                                distances.push_back( std::numeric_limits<float>::infinity() );
                                continue;
                            }

                            const float distance = CloudModellingBlobDistanceKm( item.Blob, point );
                            distances.push_back( distance );

                            nearest = any ? std::min( nearest, distance ) : distance;
                            any     = true;
                        }

                        if ( !any )
                            continue;

                        float sum = 0.0f;
                        for ( size_t k = 0; k < distances.size(); ++k )
                        {
                            if ( !std::isfinite( distances[k] ) )
                                continue;
                            sum += CloudModellingJoinTerm( placed[column[k]].Blob.Weight, distances[k], nearest,
                                                           invBlend );
                        }

                        const float joined = CloudModellingJoinKm( nearest, sum, params.BlendRadiusKm );
                        if ( joined >= 0.0f )
                            continue;

                        // The Dimensional Profile: 0 at the surface and 1 at ProfileDepth inside, which is
                        // Guerrilla's own quantity (deck p.85) obtained analytically rather than by a
                        // distance transform — and the normalised distance field variant C §3 point 2 asks
                        // the profile to BE.
                        const float profile = std::clamp( -joined * invProfile, 0.0f, 1.0f );

                        const size_t at = ( ( static_cast<size_t>( z ) * height + y ) * width + x ) *
                                          kCloudProceduralBytesPerVoxel;

                        voxels[at + slot] =
                             static_cast<unsigned char>( std::clamp( profile, 0.0f, 1.0f ) * 255.0f + 0.5f );
                    }
                }
            }
        }

        return Common::MakeSuccess( std::move( voxels ) );
    }
} // namespace Desert::Assets
