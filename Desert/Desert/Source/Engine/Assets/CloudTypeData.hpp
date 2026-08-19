#pragma once

#include <Engine/Graphic/Clouds/CloudProfileTable.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Core.hpp>
#include <Common/Core/ResultStr.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Desert::Assets
{
    /**
     * @file
     * @brief The cloud TYPE: the numbers that make one kind of cloud a different kind of cloud, the file
     *        they live in (`.decloudtype`), and the pure functions that turn one into the other.
     *
     * WHY THIS IS AN ASSET AND NOT A TABLE IN A HEADER. T0 shipped four types as a compiled-in array,
     * because it had no way to author one; the owner's request was the opposite of that — "a tool to make
     * different kinds of clouds and load them into the cloud material's slot". A type that is not a file
     * cannot be made, named, duplicated or dropped into a slot. Decision D-11 (ANALYSIS_APPROACH.md §7)
     * settles the form: a DATA asset, not a node graph, because D-5 forbade the graph and instance
     * parameters were never what it forbade.
     *
     * WHY THE PROFILE IS STILL GENERATED AND NOT STORED. Decision D-13: the runtime representation is a
     * 256 x 64 table, and the parametric curve stays as its GENERATOR. So this file carries twelve numbers
     * rather than sixteen thousand texels — small enough to read, diff and hand-edit, and the table is
     * rebuilt from it whenever the slot changes. It also means a type has no baked artefact that can go
     * stale against the maths that produced it.
     *
     * WHY JSON AND NOT A BINARY CONTAINER, when the noise volume next door is binary. The volume is eight
     * mebibytes of voxels and its header is metadata about a payload; a type IS its metadata. `.demat`
     * makes the same choice for the same reason (Engine/Assets/MaterialData.hpp), through the same
     * reflect-cpp writer, so there is one text-asset idiom in this engine rather than two.
     */

    /// The extension the Content Browser, the file dialog, the drag-and-drop target and the preloader all
    /// agree on. One constant, because a second spelling of it is a slot that silently refuses a valid file.
    inline constexpr const char* kCloudTypeExtension = ".decloudtype";

    /// The FILE layout's version, bumped when a field moves. It is not the maths' version: the profile
    /// generator lives in Engine/Graphic/Clouds/CloudProfileTable.hpp and changing it changes every type at
    /// once, which is a change to the engine rather than to any file.
    /// VERSION 2 SINCE T3. The shape gained `PlacementScale` and `PlacementAnisotropy` — where a type puts
    /// itself in the sky and how it is drawn out by the wind — and a version-1 file does not carry them.
    /// It is REFUSED rather than defaulted: a fibrous cirrus and a round-patched one are different kinds of
    /// cloud, so filling the missing numbers in silently would render a sky the file does not describe
    /// while claiming it does. The nine shipped files were rewritten in the same commit (§4.5), which is
    /// what makes the loud refusal affordable.
    inline constexpr int32_t kCloudTypeFormatVersion = 2;

    /**
     * @brief One cloud type on disk, and in memory — the same struct, because there is nothing to convert.
     *
     * The optionals are the fields a hand-written file may leave out and the engine can answer for; the
     * shape is not optional, because a type with no numbers is not a type and guessing them would produce
     * a cloud nobody authored. reflect-cpp refuses the file and names the missing field, which is the
     * behaviour §1.4 of the contract asks for.
     */
    struct CloudTypeData
    {
        /// The layout this file was written under. Absent means 1 — the first, and the only one that has
        /// ever existed.
        std::optional<int32_t> FormatVersion;

        /// What the artist calls it. Shown in the slot's dropdown and in the type panel's title; the FILE
        /// NAME stays the identity for a human, this is the identity for a reader.
        std::optional<std::string> DisplayName;

        /// A note from whoever authored it — what weather it is for, what it was derived from. Written
        /// because the shipped library's rows each have a meteorological justification and a file that
        /// loses it is a row of numbers nobody dares change.
        std::optional<std::string> Notes;

        /// The 3D noise this type's EDGE is cut from, as a path relative to the project's assets root
        /// (e.g. "Clouds/CloudNoise_FineWisp.dcnv"). Absent or empty means the built-in default volume,
        /// which is the state every type in the shipped library but one is in.
        ///
        /// RELATIVE, and that is the difference between a library that ships and one that only works on
        /// the machine it was authored on: every absolute path in this repository's scenes begins with one
        /// developer's home directory. The asset joins it to Constants::Path::ASSETS_PATH once, on load.
        std::optional<std::string> NoiseVolume;

        /// The twelve numbers. Nested rather than flattened so that the file's schema and the struct the
        /// generator consumes cannot drift apart: there is no mapping between them to get wrong.
        Graphic::CloudTypeShape Shape;
    };

    /**
     * @brief The type an EMPTY SLOT resolves to, and the one a new asset starts from.
     *
     * A scene nobody has authored a type for still has to have a sky — that is a load-bearing requirement
     * of the whole programme rather than a convenience, and it is why this is a documented answer instead
     * of a null. The row is T0's cumulus congestus, unchanged to the digit: the sky a scene had before this
     * task must be the sky it has after it, and Desert/Tests/Engine/CloudType asserts that this row and the
     * shipped Cumulus_Congestus.decloudtype are the same numbers. That assertion is the whole reason the
     * relation is written down twice — a preset table and the saved scenes disagreeing is a defect this
     * engine has already shipped once (DEV_CONTRACT.md §2.3.1, "tints do not load").
     */
    const Graphic::CloudTypeShape& CloudTypeDefaultShape();

    /// The whole default asset: the shape above, named, with no noise override and no persisted id (it is
    /// not a file, so it has no identity of its own).
    CloudTypeData CloudTypeDefault();

    /**
     * @brief Where the library lives, RELATIVE to the project's assets root.
     *
     * A scene stores the cloud type in its slot as a path relative to that root — see the CloudTypeAsset
     * branch of Core::MakeAssetResolver — which is what lets the v4 -> v5 migration name a shipped preset
     * without touching the filesystem or reading a mutable global, and stays pure while doing it.
     *
     * It must agree with Common::Constants::Path::CLOUD_TYPE_PATH, which is the same directory expressed
     * against the project root. Two statements of one directory, so Desert/Tests/Engine/CloudType asserts
     * that the second ends with the first rather than trusting them to be edited together.
     */
    inline constexpr const char* kCloudTypeAssetsRelativeDir = "Clouds/Types/";

    /// The path a scene stores for a shipped preset: the relative directory, the name, the extension.
    /// Pure and free of any global, so the migration can produce it and a test can predict it.
    inline std::string CloudTypeAssetRelativePath( std::string_view name )
    {
        std::string path = kCloudTypeAssetsRelativeDir;
        path.append( name );
        path.append( kCloudTypeExtension );
        return path;
    }

    /// The names of the shipped library, in the order the panel and the migration list them. They are the
    /// file stems too: `<name>.decloudtype` under `Resources/Assets/Clouds/Types/`.
    ///
    /// FOUR OF THEM ARE T0'S, name for name, because the v4 -> v5 migration turns the old `Species` integer
    /// into exactly these four paths and a renamed file would migrate a scene onto a type that is not there.
    inline constexpr const char* kCloudTypeStratus          = "Stratus";
    inline constexpr const char* kCloudTypeCumulusMediocris = "Cumulus_Mediocris";
    inline constexpr const char* kCloudTypeCumulusCongestus = "Cumulus_Congestus";
    inline constexpr const char* kCloudTypeCumulonimbus     = "Cumulonimbus";
    inline constexpr const char* kCloudTypeCumulusHumilis   = "Cumulus_Humilis";
    inline constexpr const char* kCloudTypeStratocumulus    = "Stratocumulus";
    inline constexpr const char* kCloudTypeAltocumulus      = "Altocumulus";
    inline constexpr const char* kCloudTypeCirrus           = "Cirrus";
    inline constexpr const char* kCloudTypeLenticular       = "Lenticular";

    /**
     * @brief Rejects a shape the generator cannot honour, with the offending number in the message.
     *
     * Pure, so the panel can refuse to save for the same reason the loader refuses to read, rather than the
     * two disagreeing about what is legal. Every refusal names the value: a NaN that reaches the profile
     * table is a black sky with nothing in the log, and it takes a day to find out which of twelve floats
     * it came from.
     */
    Common::BoolResultStr ValidateCloudTypeShape( const Graphic::CloudTypeShape& shape );

    /**
     * @brief Parses the text of a `.decloudtype`, or says why it could not.
     *
     * REFUSES RATHER THAN GUESSES. A missing shape field, a malformed document, a shape that Validate
     * rejects and an unknown format version are each an error carrying the reason. The caller (the asset,
     * or the type service) is what decides to fall back to the built-in default, and it logs that decision.
     */
    Common::ResultStr<CloudTypeData> ParseCloudType( const std::string& text );

    /// Serialises a type back to the text that ParseCloudType reads. Total: any @p data whose shape
    /// Validate accepts writes, and re-reads equal.
    std::string WriteCloudType( const CloudTypeData& data );
} // namespace Desert::Assets
