#pragma once

// DELIBERATELY FREE OF THE ENGINE AND OF Desert::Editor::Core. This header carries the STAGE MODEL — what
// the six stages are, in what order, and which subject each of them edits — and nothing else, so a suite
// can assert the mapping with no editor, no scene and no asset manager anywhere near it. The panel that
// draws it is CloudsPanel; a rule written there is a rule nothing can assert (EditorLayer.cpp and the
// panels are compiled by no suite — scripts/CI/UnreachedSources.sh).
//
// The namespace trap the neighbouring cloud headers all carry a note about applies here too: including
// Editor/Core/SubjectOpenRequest.hpp would open `Desert::Editor::Core` and silently rebind every
// unqualified `Core::Scene` / `Core::Formats` in the translation units that include this. EditorSubject.hpp
// does not open it, which is why the subject type can be named here at all.
#include <Editor/Core/EditorSubject.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/UUID.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // ── THE SKY, IN THE ORDER IT IS BUILT ──────────────────────────────────────────────────────────────
    //
    // WHY THERE IS A WINDOW AT ALL. Task O7 went looking for a defect in the noise panel and found a wider
    // one: clouds are authored in SIX places and not one of them mentions the other five. An artist who
    // wants to know where the two layout pictures live has to already know. The owner picked variant B from
    // the drawn sheets — one window, a rail of the six stages in build order, and the selected stage's own
    // editor embedded beside it — and it is Unreal's own move: their Environment Light Mixer
    // (SEnvironmentLightingViewer.cpp) gathers Sky Light, Directional Light, Sky Atmosphere, Volumetric
    // Cloud and Height Fog into one window as embedded detail views.
    //
    // THE ORDER IS THE ARGUMENT. It is not the order the formats were written in and not alphabetical: it
    // is what feeds what. The layer says where the sky is and what it may spend; the material is the whole
    // look and names everything below it; the layout says where the clouds are; the types say what kind of
    // cloud each channel is; the noise is what a type's edge is cut from; the hero bodies are the sculpted
    // clouds placed by hand over all of it. Read down the rail and you have read the chain.
    //
    // The enumerator order IS the rail order. Nothing sorts it and there is no second list of names to fall
    // behind — CloudStageName and CloudStageSubtitle switch on this enum with no `default:`, so adding a
    // stage is a compiler error at every place that has to learn about it (Assets::AssetTypeName and
    // SubjectDomainName are the same arrangement, for the same reason).
    enum class CloudStage : uint8_t
    {
        Layer,      // the VolumetricCloudComponent on an entity
        Material,   // the `.demat` that entity names
        Layout,     // the `.dclayout` the material names (pattern; the mask is the same document type)
        Types,      // the `.decloudtype` in one of the material's four slots
        Noise,      // the `.dcnv` the selected type names
        HeroBodies, // the `.dcmv` a HeroCloudComponent names

        // NOT a stage: the number of them. New stages go ABOVE this line.
        Count,
    };

    inline constexpr uint32_t kCloudStageCount = static_cast<uint32_t>( CloudStage::Count );

    /// The rail's label. No `default:` label, so a new stage reddens the build here rather than drawing a
    /// blank row; the trailing return is reached only by a value no enumerator names.
    constexpr const char* CloudStageName( const CloudStage stage ) noexcept
    {
        switch ( stage )
        {
            case CloudStage::Layer:
                return "Layer";
            case CloudStage::Material:
                return "Material";
            case CloudStage::Layout:
                return "Layout";
            case CloudStage::Types:
                return "Types";
            case CloudStage::Noise:
                return "Noise";
            case CloudStage::HeroBodies:
                return "Hero bodies";
            case CloudStage::Count:
                return "count";
        }
        return "unknown";
    }

    /// The line under the label — what this stage IS, in the fewest words that distinguish it from the
    /// other five. These are the sheet's own words, kept because they were written to be read at a glance.
    constexpr const char* CloudStageSubtitle( const CloudStage stage ) noexcept
    {
        switch ( stage )
        {
            case CloudStage::Layer:
                return "where it is, what it may spend";
            case CloudStage::Material:
                return "the whole look";
            case CloudStage::Layout:
                return "pattern + mask, seen from above";
            case CloudStage::Types:
                return "vertical profile per species";
            case CloudStage::Noise:
                return "one volume, four channels";
            case CloudStage::HeroBodies:
                return "sculpted clouds placed by hand";
            case CloudStage::Count:
                return "";
        }
        return "";
    }

    /// The four cloud type slots of a layer's material. Mirrors ECS::kCloudTypeSlots, written out rather
    /// than included because this header must not drag Engine/ECS into everything that draws a rail;
    /// CloudStagesAgreeWithTheEngine (Desert/Tests/Editor/CloudStages) asserts the two are equal.
    inline constexpr uint32_t kCloudStageTypeSlots = 4u;

    /// One hero cloud in the scene: the entity that carries it and the body it names.
    struct CloudHeroBody
    {
        Common::UUID        Entity;
        std::string         EntityName;
        Common::AssetHandle Volume; // null when the slot is empty — a body nobody sculpted is not a default one
    };

    // ── WHAT THE WHOLE SKY IS MADE OF, IN ONE VALUE ────────────────────────────────────────────────────
    //
    // Gathered ONCE per frame by the panel — from the scene's VolumetricCloudComponent and from the
    // material chain resolved exactly as the renderer resolves it (schema defaults, then the `.demat`) —
    // and then only READ. A plain struct with no engine types in it, so the mapping below can be driven by
    // a test.
    //
    // EVERY HANDLE HERE IS DERIVED, NEVER AUTHORED IN THIS WINDOW. The rail shows what the chain says; the
    // one place a slot is BOUND is the material's own Inputs table (stage 2), which is the setter every
    // widget already calls. A second picker here would be a second execution path over one value, and the
    // two would part company the day somebody adds a step to the material's — the undo entry, the publish,
    // the dirty derivation.
    struct CloudChain
    {
        /// Is there a cloud layer in this scene at all? False means every stage is empty, and the window
        /// says THAT rather than drawing six rows of "None" — a scene with no clouds and a scene whose
        /// clouds are unauthored are different facts (§1.4).
        bool HasLayer = false;

        Common::UUID LayerEntity;
        std::string  LayerName;

        Common::AssetHandle Material;
        Common::AssetHandle CloudTypes[kCloudStageTypeSlots];
        Common::AssetHandle LayoutPattern;
        Common::AssetHandle LayoutMask;

        /// The noise volume the SELECTED type names, already resolved to a handle by the panel. It is a
        /// PATH inside the `.decloudtype` (CloudTypeData::NoiseVolume), and resolving a path needs the
        /// asset manager — which this struct deliberately does not have.
        Common::AssetHandle NoiseVolume;

        std::vector<CloudHeroBody> HeroBodies;

        /// Which of the four type slots stage 4 is showing, and which hero body stage 6 is showing. They
        /// live here rather than beside the drawing code because they are half of what decides a stage's
        /// SUBJECT, and the mapping below has to be assertable without a panel.
        uint32_t SelectedTypeSlot = 0;
        uint32_t SelectedHero     = 0;
    };

    /// The AssetTypeIDs the stages resolve to. Taken as uint32 for the reason AssetSubject takes one:
    /// this header stays clear of Engine/Assets/Common.hpp. The panel passes the real enumerators and
    /// Desert/Tests/Editor/CloudStages asserts they are the ones the editors are registered under.
    struct CloudStageAssetTypes
    {
        uint32_t Material             = 0;
        uint32_t CloudLayout          = 0;
        uint32_t CloudType            = 0;
        uint32_t CloudNoiseVolume     = 0;
        uint32_t CloudModellingVolume = 0;
    };

    // ── WHICH SUBJECT A STAGE EDITS ────────────────────────────────────────────────────────────────────
    //
    // THE ONE STATEMENT OF THE MAPPING. Sheet 13 of the O9 drawings wrote it out as an argument; this is
    // the same mapping as code, and the window, the Details round trip and the suite all read it from here
    // rather than each spelling it again.
    //
    //   component header -> 1 Layer          Material slot -> 2 Material
    //   Global Pattern   -> 3 Layout         Cloud Type 1-4 -> 4 Types
    //   Noise Volume     -> 5 Noise          Hero body      -> 6 Hero bodies
    //
    // A NULL SUBJECT IS AN ANSWER, not a failure: it means the stage has nothing in it, and the window
    // offers Create instead of an editor — the Light Mixer's own move. It is distinguishable from "there
    // is no cloud layer at all" through CloudChain::HasLayer, because those are different things to tell a
    // person.
    //
    // STAGE 1 HAS NO ASSET AND ITS SUBJECT IS A COMPONENT, which is exactly what task U7 made expressible:
    // while a subject was an AssetHandle, the layer could not be named at all and the window would have
    // had to start at the material. The component type name is spelled at this one call site on purpose —
    // ComponentSubject's own note asks for it, so the identity is visible where it is built.
    [[nodiscard]] inline SubjectId CloudStageSubject( const CloudStage stage, const CloudChain& chain,
                                                      const CloudStageAssetTypes& types )
    {
        if ( !chain.HasLayer )
            return SubjectId{};

        switch ( stage )
        {
            case CloudStage::Layer:
                return ComponentSubject( chain.LayerEntity, "VolumetricCloudComponent" );

            case CloudStage::Material:
                return AssetSubject( chain.Material, types.Material );

            case CloudStage::Layout:
                return AssetSubject( chain.LayoutPattern, types.CloudLayout );

            case CloudStage::Types:
                return chain.SelectedTypeSlot < kCloudStageTypeSlots
                            ? AssetSubject( chain.CloudTypes[chain.SelectedTypeSlot], types.CloudType )
                            : SubjectId{};

            case CloudStage::Noise:
                return AssetSubject( chain.NoiseVolume, types.CloudNoiseVolume );

            case CloudStage::HeroBodies:
                return chain.SelectedHero < chain.HeroBodies.size()
                            ? AssetSubject( chain.HeroBodies[chain.SelectedHero].Volume,
                                            types.CloudModellingVolume )
                            : SubjectId{};

            case CloudStage::Count:
                return SubjectId{};
        }
        return SubjectId{};
    }

    /// Does this stage embed an ASSET DOCUMENT, or draw something of its own?
    ///
    /// Stage 1 is the odd one and it is odd for a reason rather than by omission: the layer is a COMPONENT,
    /// nothing is registered to open a document over it, and inventing one would be a second editor for
    /// fields Details already draws. The window draws the component's OWN registered editor instead — the
    /// same code Details runs, reached through ComponentWidgetRegistry — so there is no second copy of it.
    [[nodiscard]] constexpr bool CloudStageEmbedsDocument( const CloudStage stage ) noexcept
    {
        return stage != CloudStage::Layer && stage != CloudStage::Count;
    }

    // ── THE RELATION THE WINDOW HAS TO SATISFY ─────────────────────────────────────────────────────────
    //
    // "Every stage that embeds a document names a subject an editor is registered for, or names nothing."
    //
    // The failure this catches is the quiet one: a stage that resolves to a subject with no registered
    // editor draws an empty pane for ever and logs a warning nobody reads. Counted rather than answered
    // with a bool, so a red suite says how many and which.
    struct CloudStageCensus
    {
        uint32_t Stages = 0;
        /// Stages with something in them — a subject that names an asset or a component.
        uint32_t Filled = 0;
        /// Stages with nothing in them. NOT a failure: the window offers Create there.
        uint32_t Empty = 0;
        /// Stages whose subject nothing can open. Zero, or a rail row is a dead end.
        uint32_t Unopenable = 0;

        [[nodiscard]] bool EveryFilledStageCanBeOpened() const noexcept
        {
            return Unopenable == 0;
        }
    };

    /// @p hasEditorFor answers "is an editor registered for this subject type?" — SubjectEditorRegistry's
    /// own question, passed as a callable so this stays free of the registry and drivable by a test.
    template <typename HasEditorFor>
    [[nodiscard]] CloudStageCensus CensusOfCloudStages( const CloudChain& chain, const CloudStageAssetTypes& types,
                                                        const HasEditorFor& hasEditorFor )
    {
        CloudStageCensus census;
        for ( uint32_t i = 0; i < kCloudStageCount; ++i )
        {
            const auto stage = static_cast<CloudStage>( i );
            ++census.Stages;

            const SubjectId subject = CloudStageSubject( stage, chain, types );
            if ( subject.IsNull() )
            {
                ++census.Empty;
                continue;
            }

            ++census.Filled;
            if ( CloudStageEmbedsDocument( stage ) && !hasEditorFor( subject.Type() ) )
                ++census.Unopenable;
        }
        return census;
    }
} // namespace Desert::Editor
