#pragma once

#include <Engine/Assets/CloudLayout.hpp>
#include <Engine/Assets/CloudModellingVolume.hpp>
#include <Engine/Assets/CloudNoiseVolume.hpp>
#include <Engine/Assets/CloudTypeData.hpp>

#include <Common/Core/ResultStr.hpp>

#include <atomic>
#include <string>
#include <string_view>

namespace Desert::Editor::NewCloudAsset
{
    /**
     * @file
     * @brief WHAT A FRESHLY CREATED CLOUD ASSET CONTAINS — the four formats' starting states, as pure
     *        functions of nothing.
     *
     * WHY THIS IS NOT INSIDE FileExplorerPanel.cpp, which is the only caller. That translation unit pulls
     * ImGui, the renderer and the whole editor in with it, so anything that lives there is untestable by
     * construction — and the thing worth testing here is precisely the DATA: that each default encodes,
     * that it decodes back equal, and that it is the engine's own default rather than a second table
     * somebody typed into a panel. Desert/Tests/Editor/NewCloudAsset asserts exactly that, and it can
     * because these four functions know nothing about a browser, a window or a mouse.
     *
     * EVERY NUMBER BELOW COMES FROM THE ENGINE. `CloudNoiseVolumeParams` and `CloudModellingVolumeRecipe`
     * arrive by their own member initialisers, and the cloud type by `Assets::CloudTypeDefault()`. There
     * is no constant in this header that describes a cloud; the one constant there is describes a blank
     * PICTURE, and it says where its value came from.
     *
     * TWO OF THE FOUR ARE EXPENSIVE, and the difference is not a matter of degree — it decides how the
     * caller has to behave. Measured on this machine (Apple M-series, 10 cores, shared with other work),
     * Debug build, minimum of three runs:
     *
     *   | format       | cost to CREATE      | spread over three runs |
     *   |--------------|---------------------|------------------------|
     *   | .decloudtype | below the clock     | -                      |
     *   | .dclayout    | below the clock     | -                      |
     *   | .dcnv        | 8 730 ms            | 255 ms (2.9 %)         |
     *   | .dcmv        | 1 585 ms            | 9 ms (0.6 %)           |
     *
     * The two volumes ARE their voxels — `CloudNoiseVolumeAsset::Save` refuses a file whose payload does
     * not match its resolution, so "write the parameters now and bake later" is not a file that exists —
     * and eight seconds inside a menu item's handler is a frozen editor, so the caller runs them on a
     * worker. That is why the two of them take a progress hook and the two cheap ones do not.
     */

    /// Side, in texels, of the blank painting a new `.dclayout` starts as.
    ///
    /// THE SAME 512 CloudLayoutPanel's "New canvas" defaults to, and its reason is the one recorded there:
    /// at the shipped 48 km region a 512 table puts one texel at 94 m, a thirtieth of a placement cell, so
    /// a stroke has room to be measured rather than quantised. The two are not a relation that has to hold
    /// — a layout carries its own resolution and any legal side loads — they are the same editorial answer
    /// to the same question, and this one is stated here rather than reached into the panel for, because a
    /// panel's private member is not an interface.
    inline constexpr uint32_t kNewLayoutSide = 512u;

    /**
     * @brief The type a new `.decloudtype` carries.
     *
     * @param displayName what to call it — the new file's stem, which is the name a human already chose.
     *
     * TWO FIELDS OF `Assets::CloudTypeDefault()` CANNOT BE COPIED INTO A FILE VERBATIM, and that is a
     * property of the built-in rather than a re-authored default: its note reads "It is not a file", which
     * stops being true the moment it is written to one, and its display name is "Cumulus congestus
     * (built-in)", which would make every asset an artist ever creates indistinguishable in the type
     * slot's dropdown. The SHAPE — every number that makes this a kind of cloud — is untouched.
     */
    Assets::CloudTypeData DefaultType( std::string_view displayName );

    /**
     * @brief The painting a new `.dclayout` carries: a blank canvas, not an empty file.
     *
     * AN EMPTY LAYOUT IS NOT A LEGAL FILE. `CloudLayoutData` default-constructs to resolution 0 with
     * neither table, and `ValidateCloudLayoutData` refuses exactly that by name — "a layout that cannot
     * move anything is a slot an artist fills and never sees" — so `EncodeCloudLayout` would not write it
     * and `DecodeCloudLayout` would not read it back. What a new one starts as is therefore the state the
     * layout panel's own "New canvas" button produces, built through the same two engine functions in the
     * same order, so that a layout created in the browser and one baked from a fresh canvas are the same
     * bytes rather than two things that merely resemble each other.
     */
    Common::ResultStr<Assets::CloudLayoutData> DefaultLayout();

    /**
     * @brief The parameters a new `.dcnv` is generated with.
     *
     * A one-line function and it earns its place: it is where "a new noise volume is the STRUCT's own
     * defaults" is stated, so a future panel that wanted a different resolution would have to change this
     * rather than quietly grow a second table of periods beside the one in CloudNoiseVolume.hpp.
     */
    Assets::CloudNoiseVolumeParams DefaultNoiseParams();

    /**
     * @brief Generates the volume a new `.dcnv` carries. 8.7 s in a Debug build — see the file comment.
     *
     * @param progress optional, written with the fraction of slices finished; read from another thread,
     *                 hence atomic. It is what lets a caller show that the file is still being made.
     */
    Common::ResultStr<Assets::CloudNoiseVolumeData> DefaultNoiseVolume( std::atomic<float>* progress );

    /**
     * @brief Bakes the body a new `.dcmv` carries. 1.6 s in a Debug build — see the file comment.
     *
     * THE SHIPPED EXAMPLE RECIPE AND NOT AN EMPTY BOX, for the reason CloudModellingVolumePanel starts
     * from it: `ValidateCloudModellingRecipe` refuses a recipe with no lumps in it ("an empty box, not a
     * cloud"), so an empty body is not a file that can exist. The congestus is the engine's own answer to
     * "a tool that has not been given anything still has to have something to show".
     *
     * @param onProgress told how far the bake has got and asked whether to carry on; returning false
     *                   abandons it, which is what keeps a caller's shutdown bounded.
     */
    Common::ResultStr<Assets::CloudModellingVolumeData>
    DefaultModellingVolume( const Assets::CloudModellingBakeProgressFn& onProgress );
} // namespace Desert::Editor::NewCloudAsset
