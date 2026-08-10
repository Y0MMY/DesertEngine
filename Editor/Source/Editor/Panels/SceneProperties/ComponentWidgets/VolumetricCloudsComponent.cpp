#include <Editor/Core/Commands/SceneCommands.hpp>
#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/CloudPresets.hpp>
#include <Engine/Graphic/CloudQuality.hpp>

namespace Desert::Editor
{
    namespace
    {
        using Data = ::Desert::ECS::VolumetricCloudData;

        // Applies @p mutate as ONE undo step covering the whole component.
        //
        // The reflected grid records undo per field: it copies the bytes of the widget you touched. That
        // is right for a slider and wrong for a preset, which rewrites 78 fields at once — undoing the
        // one-byte enum would leave the values of the preset behind and the name of the old one in front,
        // a state the artist never authored. Snapshotting the entity instead makes a single Ctrl+Z put
        // everything back, which is what CLD-52 asks for and what the engine's own component add/remove
        // already does (ComponentEditor.cpp).
        void ApplyAsOneUndoStep( ECS::Entity& entity, const std::function<void()>& mutate )
        {
            Commands::MutateEntityUndoable( entity.GetComponent<ECS::UUIDComponent>().UUID, mutate );
        }

        // Reconciles the two selector enums with the values around them, after the reflected grid has
        // reported an edit. Four cases, and the split between them is the whole point:
        //
        //   * the artist picked a preset  -> apply its 78 look fields, leave the 13 quality knobs alone
        //   * the artist picked a tier    -> apply its 13 knobs, leave the 78 look fields alone
        //   * a look field moved          -> re-derive the preset name (Custom unless the values are a
        //                                    preset again — dialling a value back therefore restores it)
        //   * a quality knob moved        -> re-derive the tier name, and NOT the preset name
        //
        // Deriving the names from the values, rather than clearing them whenever any widget is touched,
        // is what stops "I dropped to Medium" from also erasing "Storm" — two independent dials that a
        // combined "something changed" flag would silently couple.
        void ReconcileSelectors( ECS::Entity& entity, Data& data, const Graphic::CloudPresetValues& lookBefore,
                                 const Graphic::CloudQualityValues& qualityBefore,
                                 ECS::CloudPreset presetBefore, ECS::CloudQuality tierBefore )
        {
            if ( data.Preset != presetBefore )
            {
                // Rewind before snapshotting so the undo step captures the state the artist actually had,
                // not the one the combo already half-wrote.
                const ECS::CloudPreset picked = data.Preset;
                data.Preset                   = presetBefore;
                ApplyAsOneUndoStep( entity,
                                    [picked, &data]
                                    {
                                        Graphic::ApplyPreset( picked, data );
                                        data.Preset = picked;
                                    } );
                return;
            }

            if ( data.QualityLevel != tierBefore )
            {
                const ECS::CloudQuality picked = data.QualityLevel;
                data.QualityLevel              = tierBefore;
                ApplyAsOneUndoStep( entity,
                                    [picked, &data]
                                    {
                                        Graphic::ApplyQuality( picked, data );
                                        data.QualityLevel = picked;
                                    } );
                return;
            }

            if ( !( Graphic::ExtractPresetValues( data ) == lookBefore ) )
                data.Preset = Graphic::MatchPreset( data );

            if ( !( Graphic::ExtractQualityValues( data ) == qualityBefore ) )
                data.QualityLevel = Graphic::MatchQuality( data );
        }
    } // namespace

    // Every one of the 95 cloud fields is drawn from its REFLECT()/PROPERTY() metadata — there is no
    // hand-written cloud slider anywhere. This entry exists only for what reflection cannot express: that
    // two of those fields are SELECTORS whose value has to be pushed into the other ninety-one, and that
    // three of them are noise SEEDS whose change has to reach the generation pass.
    //
    // Written out rather than using DESERT_REGISTER_CUSTOM_COMPONENT because that macro cannot fill
    // ReflectedTypeName / DataPtr, and without them the collapsed-header summary and the Details search
    // filter would silently skip this component.
    ComponentEditorEntry MakeVolumetricCloudsEntry()
    {
        using C = ::Desert::ECS::VolumetricCloudsComponent;
        ComponentEditorEntry e;
        e.Name              = "Volumetric Clouds";
        e.CanRemove         = true;
        e.ReflectedTypeName = "VolumetricCloudData";
        e.Has               = []( ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add               = []( ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove            = []( ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.DataPtr           = []( ECS::Entity& en ) -> void* { return &en.GetComponent<C>().Data; };
        e.Draw = []( ECS::Entity& en, ::Desert::Core::Scene* scene, const ComponentEditContext& ctx )
        {
            (void)scene;
            auto& clouds = en.GetComponent<C>();
            Data& data   = clouds.Data;

            const Graphic::CloudPresetValues  lookBefore    = Graphic::ExtractPresetValues( data );
            const Graphic::CloudQualityValues qualityBefore = Graphic::ExtractQualityValues( data );
            const ECS::CloudPreset            presetBefore  = data.Preset;
            const ECS::CloudQuality           tierBefore    = data.QualityLevel;
            const int                         shapeSeed     = data.ShapeSeed;
            const int                         detailSeed    = data.DetailSeed;
            const int                         weatherSeed   = data.WeatherSeed;

            const bool changed = PropertyEditorBuilder::Draw( &data, "VolumetricCloudData", ctx.AssetMgr(),
                                                              ctx.UIHelper, ctx.FieldFilter );
            if ( !changed )
                return;

            ReconcileSelectors( en, data, lookBefore, qualityBefore, presetBefore, tierBefore );

            // The three seeds do not reach the GPU as numbers — they choose which noise volumes get
            // generated, so changing one has no effect at all until the volumes are rebuilt. Raising the
            // request here is what makes them live settings rather than three sliders that appear to do
            // nothing. A preset changes seeds too, hence the check after the reconcile rather than before.
            if ( data.ShapeSeed != shapeSeed || data.DetailSeed != detailSeed ||
                 data.WeatherSeed != weatherSeed )
                clouds.RequestRegenerateNoise = true;
        };
        return e;
    }
} // namespace Desert::Editor

namespace
{
    const int _desert_volumetric_clouds_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register(
              ::Desert::Editor::MakeVolumetricCloudsEntry() );
} // namespace
