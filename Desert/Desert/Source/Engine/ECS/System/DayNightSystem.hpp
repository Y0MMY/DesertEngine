#pragma once

#include <Engine/ECS/System/System.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Core/Scene.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Desert::ECS
{
    // Drives an OPT-IN day/night cycle (SceneSettings::EnableDayNight). When on, it moves the scene's
    // directional light (the "sun") along a daily arc from SceneSettings::TimeOfDay and fades its intensity to
    // zero at night. Everything else — sky day/sunset/night colours, sky IBL — already follows the sun's
    // elevation, so this one system makes the whole environment cycle. Default off => existing scenes and any
    // hand-placed sun are left exactly as authored.
    //
    // Convention (see the DirectionalLight note): the light's DIRECTION lives in TransformComponent.Translation
    // (the direction the light travels, i.e. FROM the sun toward the scene), NOT its Rotation.
    class DayNightSystem final : public System
    {
    public:
        explicit DayNightSystem( Core::Scene* scene ) : m_Scene( scene )
        {
        }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer&,
                     const Common::Timestep& ts ) override
        {
            if ( !m_Scene )
                return;

            auto& settings = m_Scene->GetSettings();
            if ( !settings.EnableDayNight )
                return;

            // Auto-advance the clock (DayLengthSeconds real seconds == one 24h cycle). 0 = frozen: the user
            // scrubs TimeOfDay by hand in Scene Settings.
            if ( settings.DayLengthSeconds > 0.0f )
            {
                settings.TimeOfDay += ts.GetSeconds() * ( 24.0f / settings.DayLengthSeconds );
                settings.TimeOfDay = std::fmod( settings.TimeOfDay, 24.0f );
                if ( settings.TimeOfDay < 0.0f )
                    settings.TimeOfDay += 24.0f;
            }

            // Map the hour to a sun arc: a = 0 at sunrise (06:00, east), pi/2 at noon (overhead), pi at sunset
            // (18:00, west). sin(a) = elevation (negative at night), cos(a) = east(+)/west(-) sweep.
            const float a         = glm::pi<float>() * ( settings.TimeOfDay - 6.0f ) / 12.0f;
            const float elevation = std::sin( a );

            // Direction FROM the sun toward the scene = -towardSun. The small constant z tilts the arc so the
            // sun isn't a perfectly vertical plane (a more natural, slightly-south path).
            const glm::vec3 towardSun = glm::normalize( glm::vec3( std::cos( a ), std::max( elevation, -1.0f ), 0.35f ) );
            const glm::vec3 lightDir  = -towardSun;

            // Smooth day factor: full sun when up, 0 through night (with a soft dusk/dawn band around the
            // horizon so the transition isn't a hard cut).
            const float dayFactor = glm::clamp( ( elevation + 0.10f ) / 0.25f, 0.0f, 1.0f );

            auto view = registry.view<DirectionLightComponent, TransformComponent>();
            for ( auto entity : view )
            {
                auto& transform = view.get<TransformComponent>( entity );
                auto& light     = view.get<DirectionLightComponent>( entity );

                transform.Translation = lightDir;
                light.Data.Intensity  = settings.SunPeakIntensity * dayFactor;
                break; // single sun — the first directional light
            }
        }

    private:
        Core::Scene* m_Scene = nullptr;
    };
} // namespace Desert::ECS
