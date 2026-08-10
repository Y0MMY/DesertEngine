#include <Editor/Core/ImGuiUtilities.hpp>

#include <ImGui/imgui.h>

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Editor/Panels/PropertyEditor/PropertyEditorBuilder.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Scene.hpp>

#include <cmath>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        ImU32 ToU32( const glm::vec3& c )
        {
            return ImGui::ColorConvertFloat4ToU32( ImVec4( c.r, c.g, c.b, 1.0f ) );
        }

        // A ramp of the sky's OWN authored colours (zenith -> horizon -> ground) with the sun marked at
        // its current elevation. Deliberately NOT a render of the sky shader: it is a legend for the
        // colour fields below, readable while you drag them, and it costs nothing.
        void DrawSkyRamp( const ECS::SkyAtmosphereData& sky, float sunElevationDeg, bool haveSun )
        {
            const float  width  = ImGui::GetContentRegionAvail().x;
            const float  height = 96.0f;
            const ImVec2 p0     = ImGui::GetCursorScreenPos();
            const ImVec2 p1( p0.x + width, p0.y + height );
            ImDrawList*  dl = ImGui::GetWindowDrawList();

            // Horizon sits 2/3 down, like the sky itself: most of the frame is sky.
            const float horizonY = p0.y + height * 0.66f;

            // Low sun washes the horizon band with the sunset tint — the same idea the sky shader uses,
            // so the strip tracks the setting you are actually editing.
            const float     sunset  = haveSun ? glm::clamp( 1.0f - sunElevationDeg / 20.0f, 0.0f, 1.0f ) : 0.0f;
            const glm::vec3 horizon = glm::mix( sky.HorizonColor, sky.SunsetColor, sunset * 0.8f );

            dl->AddRectFilledMultiColor( p0, ImVec2( p1.x, horizonY ), ToU32( sky.ZenithColor ),
                                         ToU32( sky.ZenithColor ), ToU32( horizon ), ToU32( horizon ) );
            dl->AddRectFilled( ImVec2( p0.x, horizonY ), p1, ToU32( sky.GroundColor ) );
            dl->AddLine( ImVec2( p0.x, horizonY ), ImVec2( p1.x, horizonY ), IM_COL32( 0, 0, 0, 90 ) );
            dl->AddRect( p0, p1, ImGui::GetColorU32( ImGuiCol_Border ) );

            if ( haveSun )
            {
                // Elevation 90 = top of the strip, 0 = the horizon line, below that = under the ground.
                const float  t = glm::clamp( sunElevationDeg / 90.0f, -1.0f, 1.0f );
                const float  y = t >= 0.0f ? glm::mix( horizonY, p0.y, t ) : glm::mix( horizonY, p1.y, -t );
                const ImVec2 c( p0.x + width * 0.5f, y );
                if ( sunElevationDeg >= 0.0f )
                {
                    dl->AddCircleFilled( c, 9.0f, ToU32( sky.SunColor ), 20 );
                    dl->AddCircle( c, 9.0f, IM_COL32( 0, 0, 0, 60 ), 20 );
                }
                else
                {
                    dl->AddCircle( c, 9.0f, IM_COL32( 150, 160, 190, 200 ), 20, 2.0f );
                }
            }

            ImGui::Dummy( ImVec2( width, height ) );
            if ( !haveSun )
                ImGui::TextDisabled( "No directional light in the scene — sun position unknown" );
        }
    } // namespace

    // All 23 fields of the atmosphere are AUTO-GENERATED from its REFLECT()/PROPERTY() metadata
    // (PropertyEditorBuilder). This entry is custom only for the two things reflection cannot express:
    // the colour ramp — which needs the scene's sun elevation, and that lives in the sun light's transform,
    // not in a field — and the IBL bake action.
    //
    // Written out rather than using DESERT_REGISTER_CUSTOM_COMPONENT because that macro cannot fill
    // ReflectedTypeName / DataPtr, and without them the collapsed-header summary (the Summary fields) and
    // the Details search filter would silently skip this component.
    ComponentEditorEntry MakeSkyAtmosphereEntry()
    {
        using C = ::Desert::ECS::SkyAtmosphereComponent;
        ComponentEditorEntry e;
        e.Name              = "Sky Atmosphere";
        e.CanRemove         = true;
        e.ReflectedTypeName = "SkyAtmosphereData";
        e.Has               = []( ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add               = []( ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove            = []( ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.DataPtr           = []( ECS::Entity& en ) -> void* { return &en.GetComponent<C>().Data; };
        e.Draw              = []( ECS::Entity& en, ::Desert::Core::Scene* scene, const ComponentEditContext& ctx )
        {
            auto& atmosphere = en.GetComponent<C>();

            // The sun's elevation comes from the scene's directional light (its Translation is the
            // direction light TRAVELS, so the sun is the other way).
            float sunElevation = 0.0f;
            bool  haveSun      = false;
            if ( scene )
            {
                auto view = scene->GetRegistry().view<ECS::DirectionLightComponent, ECS::TransformComponent>();
                for ( const auto lightEntity : view )
                {
                    const glm::vec3 travel = view.template get<ECS::TransformComponent>( lightEntity ).Translation;
                    if ( glm::length( travel ) > 1e-4f )
                    {
                        const glm::vec3 toSun = -glm::normalize( travel );
                        sunElevation          = glm::degrees( std::asin( glm::clamp( toSun.y, -1.0f, 1.0f ) ) );
                        haveSun               = true;
                        break;
                    }
                }
            }

            if ( !ctx.FieldFilter )
                DrawSkyRamp( atmosphere.Data, sunElevation, haveSun );

            PropertyEditorBuilder::Draw( &atmosphere.Data, "SkyAtmosphereData", ctx.AssetMgr(), ctx.UIHelper,
                                         ctx.FieldFilter );

            // ── Environment lighting (IBL) bake — the sky's contribution to scene lighting/reflections is
            //    baked into cubemaps; this is a heavy device-idle op, so it's an explicit action. ──
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted( "Environment Lighting (IBL)" );
            ImGui::TextDisabled( "Bake the sky into the irradiance / reflection maps used by PBR surfaces." );
            if ( ImGui::Button( "Bake Sky IBL", ImVec2( ImGui::GetContentRegionAvail().x, 0 ) ) )
                atmosphere.RequestBake = true;
        };
        return e;
    }
} // namespace Desert::Editor

namespace
{
    const int _desert_sky_atmosphere_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeSkyAtmosphereEntry() );
} // namespace
