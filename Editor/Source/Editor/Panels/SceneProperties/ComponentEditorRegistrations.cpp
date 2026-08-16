// Reflected component editors: the full Details UI is auto-built from each data block's REFLECT()
// metadata (PropertyEditorBuilder) — no widget class, no edit to ComponentEditor. To expose a new
// reflected component in the editor, copy one line below.

#include <Editor/Panels/PropertyEditor/ComponentWidgetRegistry.hpp>
#include <Editor/Panels/UI/UIAnchorControls.hpp>
#include <Editor/Core/DragPayloads.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Font/FontService.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Editor/Import/MeshDnD.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>

#include <ImGui/imgui.h>
#include <glm/glm.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ThemeManager.hpp>
#include <Editor/Import/TextureDnD.hpp>
#include <Editor/Core/ColliderFit.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Core/Serialize/ComponentRegistry.hpp>
#include <Engine/Animation/AnimationLibrary.hpp>
#include <Engine/Scripting/ScriptEngine.hpp>
#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>

// Directional light is a CUSTOM entry: the reflected fields PLUS a sun dial, because the sun's direction
// is not a field — it hides in TransformComponent.Translation. See MakeDirectionalLightEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::PointLightComponent, Data, "PointLightData", "Point Light" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::SpotLightComponent, Data, "SpotLightData", "Spot Light" )
// Camera is a CUSTOM entry: reflected fields + a focal-length readout and "look through". See MakeCameraEntry.
// Terrain is a CUSTOM entry: reflected TerrainData UI + the terrain MATERIAL editor (terrain
// has no mesh material slots, so its shader/params live on the entity's MaterialComponent —
// edited HERE, inside the Terrain section, not as a separate confusing component).
// See MakeTerrainEntry below.
// Collider is registered as a CUSTOM component below (auto-fit to mesh bounds on add) instead of the
// plain reflected one-liner — see MakeColliderEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::RigidBodyComponent, Data, "RigidBodyData", "Rigid Body" )
// Character Controller is a CUSTOM entry: the reflected capsule fields PLUS the live state the physics
// step writes back (on ground / speed / swimming). Those are the values you actually need while the game
// runs, and they were invisible. See MakeCharacterControllerEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::AudioSourceComponent, Data, "AudioSourceData", "Audio Source" )
// Particle Emitter is a CUSTOM entry: the reflected fields plus a transport (play / pause / restart),
// because "is it emitting right now" is a state you drive, not a value you type. See MakeEmitterEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UICanvasComponent, Data, "UICanvasData", "UI Canvas" )
// UI Layout is a CUSTOM entry (not the reflected one-liner) so the Details panel gets Unity-style anchor
// presets ("Fill / Match Parent" + a 4x4 grid) above the raw anchor/offset fields. See MakeUILayoutEntry.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIPanelComponent, Data, "UIPanelData", "UI Panel" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UITextComponent2D, Data, "UITextData", "UI Text" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIButtonComponent, Data, "UIButtonData", "UI Button" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIIconComponent, Data, "UIIconData", "UI Icon" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIBindingComponent, Data, "UIBindingData", "UI Binding" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIScreenComponent, Data, "UIScreenData", "UI Screen" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIScreenStackComponent, Data, "UIScreenStackData",
                                     "UI Screen Stack" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UITweenComponent, Data, "UITweenData", "UI Tween" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIPointerEventsComponent, Data, "UIPointerEventsData",
                                     "UI Pointer Events" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIDraggableComponent, Data, "UIDraggableData", "UI Draggable" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIDropTargetComponent, Data, "UIDropTargetData",
                                     "UI Drop Target" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIImageComponent, Data, "UIImageData", "UI Image" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UILayoutGroupComponent, Data, "UILayoutGroupData",
                                     "UI Layout Group" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIProgressBarComponent, Data, "UIProgressBarData",
                                     "UI Progress Bar" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIToggleComponent, Data, "UIToggleData", "UI Toggle" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UISliderComponent, Data, "UISliderData", "UI Slider" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIScrollViewComponent, Data, "UIScrollViewData",
                                     "UI Scroll View" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIInputFieldComponent, Data, "UIInputFieldData",
                                     "UI Input Field" )
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::UIDropdownComponent, Data, "UIDropdownData", "UI Dropdown" )
// Exponential Height Fog is the plain reflected one-liner: every field is a value, nothing needs the
// scene, and the fog height deliberately is not a field (it is the entity's transform Y).
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::ExponentialHeightFogComponent, Data,
                                     "ExponentialHeightFogData", "Exponential Height Fog" )
// Cloud Volume is the plain reflected one-liner as well: a .dvol slot and three per-instance numbers.
// Its size and position deliberately are not fields — they are the entity's transform, exactly as the
// fog height is (teamlead Q2), so there is nothing here for a custom widget to draw.
DESERT_REGISTER_REFLECTED_COMPONENT( ::Desert::ECS::CloudVolumeComponent, Data, "CloudVolumeData", "Cloud Volume" )
// Sky Atmosphere is a CUSTOM entry: the reflected fields PLUS the sky-colour ramp (which needs the scene's
// sun elevation, and that is not a field) and the IBL bake button. See
// ComponentWidgets/SkyAtmosphereComponent.cpp.
// Volumetric Clouds is a CUSTOM entry too: the reflected fields PLUS the preset/quality selectors, which
// have to write the fields around them, and the noise-seed change that has to reach the generation pass.
// See ComponentWidgets/VolumetricCloudsComponent.cpp.

namespace Desert::Editor
{
    // The engine has its own Desert::ImGui namespace, so an unqualified ImGui:: inside Desert::Editor
    // resolves there instead of to the library. Alias it once, like every other editor TU does.
    namespace ImGui = ::ImGui;

    // Terrain material editor. TERRAIN has no mesh material slots, so its shader + params are
    // authored on the entity's MaterialComponent — the ONE remaining authored use of that
    // component (mesh entities author materials in their slots; there the component is only a
    // runtime override channel for scripts). Schema-driven from the Terrain-domain shader, laid
    // out as a two-column table (label cell never overlaps the control).
    static void DrawTerrainMaterialWidget( ::Desert::ECS::Entity&          entity,
                                           ::Desert::Assets::AssetManager* assetMgr )
    {
        namespace ImGui = ::ImGui;
        auto& mat       = entity.GetComponent<::Desert::ECS::MaterialComponent>();

        auto* shaderService = ::Desert::Runtime::ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return;

        if ( !entity.HasComponent<::Desert::ECS::TerrainComponent>() )
        {
            // Not terrain: the component only exists as a runtime/script/legacy override here.
            ImGui::TextWrapped( "Runtime shader override ('%s'). Authored materials live in the mesh's "
                                "PBR Materials slots.",
                                mat.ShaderName.empty() ? "<none>" : mat.ShaderName.c_str() );
            if ( ImGui::Button( "Clear override (use material slots)" ) )
            {
                mat.ShaderName.clear();
                mat.Params.clear();
                mat.Textures.clear();
            }
            return;
        }

        // --- Terrain-domain shader picker ---
        const std::string preview = mat.ShaderName.empty() ? "<none>" : mat.ShaderName;
        if ( ImGui::BeginCombo( "Shader", preview.c_str() ) )
        {
            for ( const auto& name : shaderService->GetAllNames() )
            {
                auto candidate = shaderService->GetByName( name );
                if ( !candidate ||
                     candidate->GetProgramMeta().Domain != ::Desert::Core::Formats::ShaderDomain::Terrain )
                    continue;
                const bool selected = ( name == mat.ShaderName );
                if ( ImGui::Selectable( name.c_str(), selected ) && !selected )
                {
                    mat.ShaderName = name;
                    mat.Params.clear();
                }
                if ( selected )
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if ( mat.ShaderName.empty() )
        {
            ImGui::TextDisabled( "Pick a terrain shader above." );
            return;
        }

        auto shader = shaderService->GetByName( mat.ShaderName );
        if ( !shader )
            return;
        const auto& schema = shader->GetProgramMeta();
        if ( schema.Params.empty() )
            return;

        if ( !ImGui::BeginTable( "##terrain_mat", 2,
                                 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings ) )
            return;
        ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthStretch, 0.38f );
        ImGui::TableSetupColumn( "control", ImGuiTableColumnFlags_WidthStretch, 0.62f );

        const auto findOrAdd =
             [&]( const ::Desert::Core::Formats::ShaderParam& p ) -> ::Desert::ECS::MaterialParamOverride&
        {
            for ( auto& o : mat.Params )
                if ( o.Name == p.Name )
                    return o;
            mat.Params.push_back( { p.Name, p.Default } );
            return mat.Params.back();
        };

        for ( const auto& p : schema.Params )
        {
            using W  = ::Desert::Core::Formats::ShaderParamWidget;
            using VT = ::Desert::Core::Formats::ShaderValueType;

            const char*       label    = p.DisplayName.empty() ? p.Name.c_str() : p.DisplayName.c_str();
            const std::string hiddenId = "##tp_" + p.Name;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( label );
            ImGui::TableNextColumn();
            ImGui::PushItemWidth( -FLT_MIN );

            if ( p.IsTexture )
            {
                ::Desert::ECS::MaterialTextureOverride* texOv = nullptr;
                for ( auto& t : mat.Textures )
                    if ( t.Name == p.Name )
                    {
                        texOv = &t;
                        break;
                    }
                if ( !texOv )
                {
                    mat.Textures.push_back( { p.Name, 0 } );
                    texOv = &mat.Textures.back();
                }

                std::string disp = "<drop texture>";
                if ( texOv->TextureHandle != 0 && assetMgr )
                {
                    if ( auto tex = assetMgr->FindByHandle<::Desert::Assets::TextureAsset>(
                              ::Common::UUID( texOv->TextureHandle ) ) )
                    {
                        const auto& src  = tex->GetSourcePath();
                        const auto  path = !src.empty() ? src : tex->GetMetadata().Filepath.string();
                        disp             = std::filesystem::path( path ).filename().string();
                    }
                }
                ImGui::Button( ( disp + hiddenId ).c_str(), ImVec2( -FLT_MIN, 0.0f ) );
                if ( ImGui::BeginDragDropTarget() )
                {
                    if ( const ImGuiPayload* pl =
                              ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::TextureAsset ) )
                    {
                        const std::string path( static_cast<const char*>( pl->Data ),
                                                pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                        if ( assetMgr )
                        {
                            const auto resolved = ::Desert::Editor::TextureDnD::ResolveOrImport( *assetMgr, path );
                            if ( static_cast<uint64_t>( resolved ) != 0 )
                                texOv->TextureHandle = static_cast<uint64_t>( resolved );
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopItemWidth();
                continue;
            }

            auto& ov = findOrAdd( p );
            if ( p.Widget == W::Color )
            {
                if ( p.Type == VT::Float3 )
                    ImGui::ColorEdit3( hiddenId.c_str(), &ov.Value.x );
                else
                    ImGui::ColorEdit4( hiddenId.c_str(), &ov.Value.x );
            }
            else
            {
                const int comps = ( p.Type == VT::Float2 )   ? 2
                                  : ( p.Type == VT::Float3 ) ? 3
                                  : ( p.Type == VT::Float4 ) ? 4
                                                             : 1;
                if ( p.Min.has_value() && p.Max.has_value() )
                {
                    float mn = *p.Min, mx = *p.Max;
                    ImGui::SliderScalarN( hiddenId.c_str(), ImGuiDataType_Float, &ov.Value.x, comps, &mn, &mx );
                }
                else
                {
                    ImGui::DragScalarN( hiddenId.c_str(), ImGuiDataType_Float, &ov.Value.x, comps, 0.01f );
                }
            }
            ImGui::PopItemWidth();
        }
        ImGui::EndTable();
    }

    // Sizes a collider to the entity's mesh bounds (so the green wireframe wraps the visible object —
    // UE auto-fits collision to the mesh instead of leaving a default 0.5 cube). HalfExtents/Radius are
    // world units, so we multiply the local AABB by the entity's scale (PhysicsECSSystem feeds these to
    // Jolt directly, ignoring the transform's scale).
    // Collider fitting lives in Editor/Core/ColliderFit.hpp — the toolbar's Collision menu measures the
    // same mesh the same way, and a warning that disagrees with the button that silences it is worse than
    // no warning.
    using ::Desert::Editor::Core::FitColliderToMesh;
    using ::Desert::Editor::Core::MeshHalfExtents;

    // Collider editor: same auto-built reflected UI as the one-liner, PLUS a one-time auto-fit on Add and
    // a manual "Fit to Mesh Bounds" button.
    // A top-down hemisphere dial for a sun direction: the centre is straight up (elevation 90 deg), the
    // rim is the horizon, and the angle around the circle is the compass azimuth (up = +Z, right = +X).
    // Dragging moves the sun; the numeric sliders beside it stay the precise path (and are the only way
    // to put the sun BELOW the horizon, which a hemisphere cannot show).
    static bool DrawSunDial( float& azimuthDeg, float& elevationDeg, float diameter )
    {
        ImDrawList*  dl     = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float  r      = diameter * 0.5f;
        const ImVec2 c( origin.x + r, origin.y + r );

        ImGui::InvisibleButton( "##sundial", ImVec2( diameter, diameter ) );

        bool changed = false;
        if ( ImGui::IsItemActive() )
        {
            const ImVec2 m   = ImGui::GetIO().MousePos;
            const float  dx  = m.x - c.x;
            const float  dy  = m.y - c.y;
            const float  len = std::sqrt( dx * dx + dy * dy );

            azimuthDeg = glm::degrees( std::atan2( dx, -dy ) );
            if ( azimuthDeg < 0.0f )
                azimuthDeg += 360.0f;
            elevationDeg = ( 1.0f - glm::min( 1.0f, len / r ) ) * 90.0f;
            changed      = true;
        }

        const ImU32 ring = ImGui::GetColorU32( ImGuiCol_Border );
        dl->AddCircleFilled( c, r, ImGui::GetColorU32( ImGuiCol_FrameBg ), 48 );
        dl->AddCircle( c, r, ring, 48 );
        dl->AddCircle( c, r * 0.5f, ring, 32 ); // the 45 deg elevation ring
        dl->AddLine( ImVec2( c.x - r, c.y ), ImVec2( c.x + r, c.y ), ring );
        dl->AddLine( ImVec2( c.x, c.y - r ), ImVec2( c.x, c.y + r ), ring );

        const ImU32 label = ImGui::GetColorU32( ImGuiCol_TextDisabled );
        dl->AddText( ImVec2( c.x - 3.0f, c.y - r - 2.0f ), label, "N" );
        dl->AddText( ImVec2( c.x + r - 6.0f, c.y - 7.0f ), label, "E" );
        dl->AddText( ImVec2( c.x - 3.0f, c.y + r - 14.0f ), label, "S" );
        dl->AddText( ImVec2( c.x - r + 2.0f, c.y - 7.0f ), label, "W" );

        // The sun itself. Below the horizon it is pinned to the rim and drawn hollow — the dial covers
        // the sky, so "night" has to read as a state rather than a position.
        const bool   belowHorizon = elevationDeg < 0.0f;
        const float  el           = glm::clamp( elevationDeg, 0.0f, 90.0f );
        const float  rr           = ( 1.0f - el / 90.0f ) * r;
        const float  azr          = glm::radians( azimuthDeg );
        const ImVec2 sun( c.x + std::sin( azr ) * rr, c.y - std::cos( azr ) * rr );
        dl->AddLine( c, sun, ImGui::GetColorU32( ImGuiCol_TextDisabled ) );
        if ( belowHorizon )
            dl->AddCircle( sun, 6.0f, IM_COL32( 120, 130, 160, 255 ), 16, 2.0f );
        else
            dl->AddCircleFilled( sun, 6.0f, IM_COL32( 255, 210, 90, 255 ), 16 );

        return changed;
    }

    // Directional light: the reflected fields, plus the thing that is NOT a field — where the sun is.
    // The engine stores the sun as the direction light TRAVELS in TransformComponent.Translation
    // (Scene.cpp uploads normalize(Translation); the sky negates it), which is unauthorable as three
    // raw numbers. This edits it as azimuth/elevation.
    static ComponentEditorEntry MakeDirectionalLightEntry()
    {
        using C = ::Desert::ECS::DirectionLightComponent;
        ComponentEditorEntry e;
        e.Name              = "Directional Light";
        e.CanRemove         = true;
        e.ReflectedTypeName = "DirectionalLightData";
        e.Has               = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add               = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove            = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.DataPtr           = []( ::Desert::ECS::Entity& en ) -> void* { return &en.GetComponent<C>().Data; };
        e.Draw = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<C>();
            PropertyEditorBuilder::Draw( &c.Data, "DirectionalLightData", ctx.AssetMgr(), ctx.UIHelper,
                                         ctx.FieldFilter );

            if ( ctx.FieldFilter || !en.HasComponent<::Desert::ECS::TransformComponent>() )
                return; // while searching, only the matched fields are on screen

            auto& t = en.GetComponent<::Desert::ECS::TransformComponent>();

            // Translation is the TRAVEL direction; the sun sits the other way.
            glm::vec3 travel = t.Translation;
            float     length = glm::length( travel );
            if ( length < 1e-4f )
            {
                travel = glm::vec3( -0.4f, -1.0f, -0.5f );
                length = glm::length( travel );
            }
            const glm::vec3 toSun = -travel / length;

            float elevation = glm::degrees( std::asin( glm::clamp( toSun.y, -1.0f, 1.0f ) ) );
            float azimuth   = glm::degrees( std::atan2( toSun.x, toSun.z ) );
            if ( azimuth < 0.0f )
                azimuth += 360.0f;

            if ( !::Desert::Editor::Utils::ImGuiUtilities::SectionHeader( ICON_MDI_WEATHER_SUNNY
                                                                          "  Sun Direction" ) )
                return;

            ImGui::Indent( 6.0f );
            bool changed = DrawSunDial( azimuth, elevation, 120.0f );

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth( -1.0f );
            changed |= ImGui::SliderFloat( "##azimuth", &azimuth, 0.0f, 360.0f, "Azimuth %.0f deg" );
            ImGui::SetNextItemWidth( -1.0f );
            changed |= ImGui::SliderFloat( "##elevation", &elevation, -90.0f, 90.0f, "Elevation %.0f deg" );
            if ( elevation < 0.0f )
                ImGui::TextColored( ImVec4( 0.6f, 0.65f, 0.8f, 1.0f ), ICON_MDI_WEATHER_NIGHT " below horizon" );
            ImGui::EndGroup();

            if ( changed )
            {
                const float     az = glm::radians( azimuth );
                const float     el = glm::radians( elevation );
                const glm::vec3 dir( std::cos( el ) * std::sin( az ), std::sin( el ),
                                     std::cos( el ) * std::cos( az ) );
                // Keep the vector's length: some scenes author it as a "sun position" and only the
                // direction is read, so rewriting the magnitude would be a silent edit.
                t.Translation = -dir * length;
            }

            ImGui::Unindent( 6.0f );
        };
        return e;
    }

    // Camera: the reflected fields, plus the two things a camera needs that numbers alone don't give —
    // the lens in millimetres photographers think in, and a way to see what it sees.
    static ComponentEditorEntry MakeCameraEntry()
    {
        using C = ::Desert::ECS::CameraComponent;
        ComponentEditorEntry e;
        e.Name              = "Camera";
        e.CanRemove         = true;
        e.ReflectedTypeName = "CameraData";
        e.Has               = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add               = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove            = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.DataPtr           = []( ::Desert::ECS::Entity& en ) -> void* { return &en.GetComponent<C>().Data; };
        e.Draw = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene* scene, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<C>();
            PropertyEditorBuilder::Draw( &c.Data, "CameraData", ctx.AssetMgr(), ctx.UIHelper, ctx.FieldFilter );

            if ( ctx.FieldFilter )
                return;

            // Focal length <-> vertical FOV on a 35mm full-frame sensor (24mm high), the lens language
            // every reference shot is quoted in. Purely a second view of the SAME field.
            constexpr float kSensorHalfHeightMm = 12.0f;
            const float     fovRad              = glm::radians( glm::clamp( c.Data.FOV, 1.0f, 179.0f ) );
            float           focalMm             = kSensorHalfHeightMm / std::tan( fovRad * 0.5f );

            ImGui::Separator();
            ImGui::SetNextItemWidth( 180.0f );
            if ( ImGui::DragFloat( "Focal length", &focalMm, 0.5f, 4.0f, 800.0f, "%.0f mm" ) )
            {
                const float newFov =
                     2.0f * glm::degrees( std::atan( kSensorHalfHeightMm / glm::max( focalMm, 1.0f ) ) );
                c.Data.FOV = glm::clamp( newFov, 10.0f, 120.0f );
            }
            ::Desert::Editor::Utils::ImGuiUtilities::Tooltip(
                 "The same setting as Field of View, in 35mm-equivalent lens terms (24mm sensor height)." );

            // "Look through": the editor camera is moved to this camera's transform instead of the
            // viewport being handed over — nothing about the scene's active camera changes, so leaving
            // is just moving the view again.
            if ( scene && en.HasComponent<::Desert::ECS::TransformComponent>() )
            {
                if ( ImGui::Button( ICON_MDI_EYE "  Look through this camera", ImVec2( -1.0f, 0.0f ) ) )
                {
                    if ( auto* editorCam =
                              dynamic_cast<::Desert::Core::EditorCamera*>( scene->GetActiveCamera().get() ) )
                    {
                        const glm::mat4 world    = en.GetWorldTransform();
                        const glm::vec3 position = glm::vec3( world[3] );
                        const glm::vec3 forward  = -glm::normalize( glm::vec3( world[2] ) );

                        // Focus() backs the camera off along its CURRENT direction, so aim first.
                        editorCam->SnapToDirection( forward );
                        editorCam->Focus( position + forward * 100.0f, 100.0f );
                    }
                }
                ::Desert::Editor::Utils::ImGuiUtilities::Tooltip(
                     "Moves the EDITOR camera to this camera's position and orientation" );
            }
        };
        return e;
    }

    // Particle emitter: transport first, then the reflected parameters. Pause writes Enabled (the same
    // field the renderer reads, so nothing new can drift out of sync) and Restart raises the component's
    // one-shot flag that ParticleRenderer consumes next frame.
    static ComponentEditorEntry MakeEmitterEntry()
    {
        using C = ::Desert::ECS::ParticleEmitterComponent;
        ComponentEditorEntry e;
        e.Name              = "Particle Emitter";
        e.CanRemove         = true;
        e.ReflectedTypeName = "ParticleEmitterData";
        e.Has               = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add               = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove            = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.DataPtr           = []( ::Desert::ECS::Entity& en ) -> void* { return &en.GetComponent<C>().Data; };
        e.Draw = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<C>();

            if ( !ctx.FieldFilter )
            {
                const float w = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) * 0.5f;
                if ( ImGui::Button( c.Data.Enabled ? ICON_MDI_PAUSE "  Pause" : ICON_MDI_PLAY "  Play",
                                    ImVec2( w, 0.0f ) ) )
                    c.Data.Enabled = !c.Data.Enabled;
                ImGui::SameLine();
                if ( ImGui::Button( ICON_MDI_RESTART "  Restart", ImVec2( -1.0f, 0.0f ) ) )
                    c.RequestRestart = true;
                ::Desert::Editor::Utils::ImGuiUtilities::Tooltip(
                     "Kill every live particle and start emitting from scratch" );
                ImGui::Spacing();
            }

            PropertyEditorBuilder::Draw( &c.Data, "ParticleEmitterData", ctx.AssetMgr(), ctx.UIHelper,
                                         ctx.FieldFilter );
        };
        return e;
    }

    // Terrain: reflected TerrainData UI + the terrain MATERIAL (shader + schema params) in ONE
    // section. Terrain has no mesh slots, so its material lives on a MaterialComponent that this
    // entry manages implicitly — no separate component for the user to discover or confuse.
    static ComponentEditorEntry MakeTerrainEntry()
    {
        ComponentEditorEntry e;
        e.Name      = "Terrain";
        e.CanRemove = true;
        e.Has    = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<::Desert::ECS::TerrainComponent>(); };
        e.Add    = []( ::Desert::ECS::Entity& en ) { en.AddComponent<::Desert::ECS::TerrainComponent>(); };
        e.Remove = []( ::Desert::ECS::Entity& en )
        {
            en.RemoveComponent<::Desert::ECS::TerrainComponent>();
            // The terrain's material rides along (it has no meaning without the terrain).
            if ( en.HasComponent<::Desert::ECS::MaterialComponent>() )
                en.RemoveComponent<::Desert::ECS::MaterialComponent>();
        };
        e.Draw = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<::Desert::ECS::TerrainComponent>();
            PropertyEditorBuilder::Draw( &c.Data, "TerrainData", ctx.AssetMgr(), ctx.UIHelper );

            ::ImGui::Separator();
            ::ImGui::TextDisabled( "Material" );
            if ( !en.HasComponent<::Desert::ECS::MaterialComponent>() )
                en.AddComponent<::Desert::ECS::MaterialComponent>();
            DrawTerrainMaterialWidget( en, ctx.AssetMgr() );
        };
        return e;
    }

    static ComponentEditorEntry MakeColliderEntry()
    {
        ComponentEditorEntry e;
        e.Name      = "Collider";
        e.CanRemove = true;
        e.Has = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<::Desert::ECS::ColliderComponent>(); };
        e.Add = []( ::Desert::ECS::Entity& en )
        {
            auto& c = en.AddComponent<::Desert::ECS::ColliderComponent>();
            FitColliderToMesh( en, c.Data );
        };
        e.Remove = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<::Desert::ECS::ColliderComponent>(); };
        e.Draw   = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<::Desert::ECS::ColliderComponent>();
            PropertyEditorBuilder::Draw( &c.Data, "ColliderData", ctx.AssetMgr(), ctx.UIHelper, ctx.FieldFilter );

            // A collider that disagrees with the mesh it is supposed to wrap is invisible until something
            // walks into thin air — the greybox house shipped with double-size colliders for exactly this
            // reason (see the world-units commit). Say it here, next to the button that fixes it.
            if ( !ctx.FieldFilter )
            {
                if ( const auto meshHalf = MeshHalfExtents( en ) )
                {
                    const glm::vec3 colliderHalf =
                         c.Data.Shape == ::Desert::Physics::ShapeType::Box
                              ? c.Data.HalfExtents
                              : glm::vec3( c.Data.Radius,
                                           c.Data.Shape == ::Desert::Physics::ShapeType::Capsule
                                                ? c.Data.HalfHeight + c.Data.Radius
                                                : c.Data.Radius,
                                           c.Data.Radius );

                    // Relative on purpose: 5 cm matters on a doorknob and not on a hillside.
                    const glm::vec3 ref   = glm::max( *meshHalf, glm::vec3( 1.0f ) );
                    const glm::vec3 delta = glm::abs( colliderHalf - *meshHalf ) / ref;
                    const float     worst = glm::max( delta.x, glm::max( delta.y, delta.z ) );
                    if ( worst > 0.25f )
                    {
                        ImGui::PushStyleColor( ImGuiCol_Text, ::Desert::Editor::ThemeManager::GetWarningColor() );
                        ImGui::TextWrapped( ICON_MDI_ALERT " Collision is %.0f%% off the mesh bounds "
                                                           "(mesh half-extents %.0f x %.0f x %.0f cm)",
                                            worst * 100.0f, meshHalf->x, meshHalf->y, meshHalf->z );
                        ImGui::PopStyleColor();
                    }
                }
            }

            if ( ::ImGui::Button( "Fit to Mesh Bounds", ImVec2( -1.0f, 0.0f ) ) )
                FitColliderToMesh( en, c.Data );
        };
        return e;
    }

    // UI Layout (RectTransform): anchor-preset controls ("Fill / Match Parent" + 4x4 grid) on top of the
    // reflected anchor/offset/pivot fields, so you can match the parent from the inspector (not just the
    // viewport toolbar). Presets act in design space (keep the authored size; stretch fills the axis).
    static ComponentEditorEntry MakeUILayoutEntry()
    {
        using C = ::Desert::ECS::UILayoutComponent;
        ComponentEditorEntry e;
        e.Name      = "UI Layout";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<C>();
            UIAnchors::DrawControls( c.Data );
            PropertyEditorBuilder::Draw( &c.Data, "UILayoutData", ctx.AssetMgr(), ctx.UIHelper );
        };
        return e;
    }
    // UE-style Instanced Static Mesh editor: pick a primitive (or drop an asset mesh) + add/clear instances.
    // Instances are WORLD-space; all of them render as ONE instanced draw (+1 per shadow cascade).
    static ComponentEditorEntry MakeInstancedStaticMeshEntry()
    {
        using ISMC = ::Desert::ECS::InstancedStaticMeshComponent;
        ComponentEditorEntry e;
        e.Name      = "Instanced Static Mesh";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<ISMC>(); };
        e.Add       = []( ::Desert::ECS::Entity& en )
        {
            auto& c     = en.AddComponent<ISMC>();
            c.Primitive = ::Desert::Geometry::PrimitiveType::Cube; // renders immediately
            if ( c.InstanceTransforms.empty() )
                c.InstanceTransforms.push_back( glm::mat4( 1.0f ) );
        };
        e.Remove = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<ISMC>(); };
        e.Draw   = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            auto& c = en.GetComponent<ISMC>();

            // Mesh source: a built-in primitive, or an asset mesh dropped from the browser.
            static const char* kPrims[] = { "Cube", "Sphere", "Plane", "Pyramid" };
            int                cur      = c.Primitive.has_value() ? static_cast<int>( c.Primitive.value() ) : 0;
            if ( !c.MeshHandle && ::ImGui::Combo( "Primitive", &cur, kPrims, IM_ARRAYSIZE( kPrims ) ) )
            {
                c.Primitive = static_cast<::Desert::Geometry::PrimitiveType>( cur );
                c.RuntimeMesh.reset();
            }

            ::ImGui::Button( c.MeshHandle ? "Mesh: <asset> (drop to replace)"
                                          : "Drop a .stmesh to use an asset mesh",
                             ImVec2( -1.0f, 0.0f ) );
            if ( ::ImGui::BeginDragDropTarget() )
            {
                if ( const ImGuiPayload* pl =
                          ::ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::MeshAsset );
                     pl && ctx.AssetMgr() )
                {
                    const std::string path( static_cast<const char*>( pl->Data ),
                                            pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                    const auto        handle = ::Desert::Editor::MeshDnD::ResolveOrImport( *ctx.AssetMgr(), path );
                    if ( !handle.IsNull() )
                    {
                        c.MeshHandle = handle;
                        c.Primitive.reset();
                        c.RuntimeMesh.reset();
                    }
                }
                ::ImGui::EndDragDropTarget();
            }
            if ( c.MeshHandle && ::ImGui::SmallButton( "Use Primitive Instead" ) )
            {
                c.MeshHandle = {};
                c.Primitive  = ::Desert::Geometry::PrimitiveType::Cube;
            }

            ::ImGui::Separator();
            ::ImGui::Text( "Instances: %d", static_cast<int>( c.InstanceTransforms.size() ) );

            if ( ::ImGui::Button( "Add Instance" ) )
            {
                const float n = static_cast<float>( c.InstanceTransforms.size() );
                c.InstanceTransforms.push_back( glm::translate( glm::mat4( 1.0f ), glm::vec3( n * 2.0f, 0, 0 ) ) );
                c.InstancesDirty = true;
            }
            ::ImGui::SameLine();
            if ( ::ImGui::Button( "Add 10x10 Grid" ) )
            {
                for ( int z = 0; z < 10; ++z )
                    for ( int x = 0; x < 10; ++x )
                        c.InstanceTransforms.push_back(
                             glm::translate( glm::mat4( 1.0f ), glm::vec3( x * 2.0f, 0.0f, z * 2.0f ) ) );
                c.InstancesDirty = true;
            }
            ::ImGui::SameLine();
            if ( ::ImGui::Button( "Clear" ) )
            {
                c.InstanceTransforms.clear();
                c.InstancesDirty = true;
            }
        };
        return e;
    }

    // ============================================================================================
    // Components that had no Details entry at all — their data existed, was serialized and was read by
    // the systems, but could only be authored by editing the scene file. One entry each.
    // ============================================================================================

    // UE's "Sockets ▸ Parent Socket": follow a BONE of another skinned entity (weapon in hand, hat on
    // head). The bone list comes from the TARGET's skeleton, so the name can only ever be one that
    // exists — typing it by hand was the alternative.
    static ComponentEditorEntry MakeSocketEntry()
    {
        using C = ::Desert::ECS::SocketAttachmentComponent;
        ComponentEditorEntry e;
        e.Name      = "Socket";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene* scene, const ComponentEditContext& )
        {
            namespace U = ::Desert::Editor::Utils;
            auto& c     = en.GetComponent<C>();

            U::ImGuiUtilities::ResetPropertyRows();

            // --- Target: any OTHER entity carrying a skinned mesh (only those have bones) -------------
            std::string                          targetName   = "None";
            const ::Desert::ECS::Entity*         targetEntity = nullptr;
            std::optional<::Desert::ECS::Entity> targetStorage;
            if ( scene && !c.Target.IsNull() )
            {
                if ( auto ref = scene->FindEntityByID( c.Target ) )
                {
                    targetStorage = ref->get();
                    targetEntity  = &*targetStorage;
                    if ( targetStorage->HasComponent<::Desert::ECS::TagComponent>() )
                        targetName = targetStorage->GetComponent<::Desert::ECS::TagComponent>().Tag;
                }
                else
                {
                    targetName = "<missing entity>";
                }
            }

            U::ImGuiUtilities::BeginPropertyRow( "Target", "The skinned entity whose bone this follows" );
            if ( U::ImGuiUtilities::AssetSlot( "sockettarget", targetName.c_str(), c.Target.IsNull() ) )
                ImGui::OpenPopup( "socket_target" );
            if ( ImGui::BeginPopup( "socket_target" ) )
            {
                if ( ImGui::Selectable( "None (detached)" ) )
                {
                    c.Target = {};
                    c.BoneName.clear();
                }
                if ( scene )
                {
                    auto& registry = scene->GetRegistry();
                    auto view = registry.view<::Desert::ECS::SkinnedMeshComponent, ::Desert::ECS::UUIDComponent>();
                    for ( auto handle : view )
                    {
                        ::Desert::ECS::Entity candidate( handle, registry );
                        const auto            uuid = candidate.GetComponent<::Desert::ECS::UUIDComponent>().UUID;
                        const std::string     name = candidate.HasComponent<::Desert::ECS::TagComponent>()
                                                          ? candidate.GetComponent<::Desert::ECS::TagComponent>().Tag
                                                          : std::string( "Entity" );
                        if ( ImGui::Selectable( name.c_str(), uuid == c.Target ) )
                        {
                            c.Target = uuid;
                            c.BoneName.clear(); // a bone of the OLD rig means nothing on the new one
                        }
                    }
                }
                ImGui::EndPopup();
            }
            U::ImGuiUtilities::EndPropertyRow();

            // --- Bone: the target's own bone names --------------------------------------------------
            const ::Desert::Animation::Skeleton* skeleton = nullptr;
            if ( targetEntity )
            {
                const auto&     smc = targetEntity->GetComponent<::Desert::ECS::SkinnedMeshComponent>();
                ::Desert::Mesh* mesh =
                     smc.RuntimeMesh
                          ? static_cast<::Desert::Mesh*>( smc.RuntimeMesh.get() )
                          : ::Desert::Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
                if ( mesh && mesh->IsSkinned() )
                    skeleton = &static_cast<::Desert::SkinnedMesh*>( mesh )->GetSkeleton();
            }

            U::ImGuiUtilities::BeginPropertyRow( "Bone", "Bone on the target's skeleton to follow" );
            const std::string bonePreview = c.BoneName.empty() ? "None" : c.BoneName;
            if ( U::ImGuiUtilities::AssetSlot( "socketbone", bonePreview.c_str(), c.BoneName.empty() ) )
                ImGui::OpenPopup( "socket_bone" );
            if ( ImGui::BeginPopup( "socket_bone" ) )
            {
                if ( !skeleton )
                {
                    ImGui::TextDisabled( "Pick a target with a skeleton first" );
                }
                else
                {
                    static ImGuiTextFilter boneFilter;
                    boneFilter.Draw( "##bonesearch", 180.0f );
                    ImGui::Separator();
                    for ( const auto& bone : skeleton->GetBones() )
                    {
                        if ( !boneFilter.PassFilter( bone.Name.c_str() ) )
                            continue;
                        if ( ImGui::Selectable( bone.Name.c_str(), bone.Name == c.BoneName ) )
                            c.BoneName = bone.Name;
                    }
                }
                ImGui::EndPopup();
            }
            U::ImGuiUtilities::EndPropertyRow();

            // --- Grip offset (the weapon almost never sits on the bone origin) -----------------------
            U::ImGuiUtilities::BeginPropertyRow( "Offset Location", "Relative to the bone, in centimetres" );
            U::ImGuiUtilities::VectorField( "sockloc", &c.OffsetTranslation.x, 3, 0.5f, "%.1f" );
            U::ImGuiUtilities::EndPropertyRow();

            // Stored in radians like every other rotation in the engine; shown in degrees like every other
            // rotation in the editor.
            U::ImGuiUtilities::BeginPropertyRow( "Offset Rotation", "Relative to the bone, in degrees" );
            glm::vec3 socketDegrees = glm::degrees( c.OffsetRotation );
            if ( U::ImGuiUtilities::VectorField( "sockrot", &socketDegrees.x, 3, 0.5f, "%.1f\xc2\xb0" ) )
                c.OffsetRotation = glm::radians( socketDegrees );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Offset Scale" );
            U::ImGuiUtilities::VectorField( "sockscale", &c.OffsetScale.x, 3, 0.01f, "%.3f" );
            U::ImGuiUtilities::EndPropertyRow();

            if ( targetEntity && !skeleton )
                ImGui::TextDisabled( ICON_MDI_ALERT "  The target has no built skeleton yet" );
        };
        return e;
    }

    // Locomotion: the state -> clip mapping LocomotionSystem reads. The clip names are picked from the
    // animation library rather than typed, because a typo here is a character that simply never walks.
    static ComponentEditorEntry MakeLocomotionEntry()
    {
        using C = ::Desert::ECS::LocomotionComponent;
        ComponentEditorEntry e;
        e.Name      = "Locomotion";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            namespace U = ::Desert::Editor::Utils;
            auto& c     = en.GetComponent<C>();

            U::ImGuiUtilities::ResetPropertyRows();

            // The clips that fit THIS character's skeleton, if it has one; otherwise the field is still
            // editable as free text through the same popup (the library may load later).
            std::vector<std::string> clipNames;
            if ( ctx.AnimationLibrary && en.HasComponent<::Desert::ECS::SkinnedMeshComponent>() )
            {
                const auto&     smc = en.GetComponent<::Desert::ECS::SkinnedMeshComponent>();
                ::Desert::Mesh* mesh =
                     smc.RuntimeMesh
                          ? static_cast<::Desert::Mesh*>( smc.RuntimeMesh.get() )
                          : ::Desert::Runtime::ResourceRegistry::GetMeshService()->Get( smc.MeshHandle );
                if ( mesh && mesh->IsSkinned() )
                {
                    const auto& skeleton = static_cast<::Desert::SkinnedMesh*>( mesh )->GetSkeleton();
                    for ( const auto& asset : ctx.AnimationLibrary->GetBySkeleton( skeleton.GetSignature() ) )
                        if ( asset )
                            clipNames.push_back( asset->GetClip().AnimationName );
                }
            }

            const auto clipRow = [&clipNames]( const char* label, std::string& value, const char* id )
            {
                U::ImGuiUtilities::BeginPropertyRow( label );
                if ( U::ImGuiUtilities::AssetSlot( id, value.empty() ? "None" : value.c_str(), value.empty() ) )
                    ImGui::OpenPopup( id );
                if ( ImGui::BeginPopup( id ) )
                {
                    if ( clipNames.empty() )
                        ImGui::TextDisabled( "No clips for this skeleton" );
                    for ( const auto& name : clipNames )
                        if ( ImGui::Selectable( name.c_str(), name == value ) )
                            value = name;
                    ImGui::EndPopup();
                }
                U::ImGuiUtilities::EndPropertyRow();
            };

            clipRow( "Idle Clip", c.IdleClip, "loco_idle" );
            clipRow( "Walk Clip", c.WalkClip, "loco_walk" );
            clipRow( "Run Clip", c.RunClip, "loco_run" );
            clipRow( "Jump Clip", c.JumpClip, "loco_jump" );

            U::ImGuiUtilities::BeginPropertyRow( "Walk Speed", "Planar speed above which the walk clip plays" );
            ImGui::DragFloat( "##walkspeed", &c.WalkSpeed, 0.01f, 0.0f, 100.0f, "%.2f" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Run Speed", "Planar speed above which the run clip plays" );
            ImGui::DragFloat( "##runspeed", &c.RunSpeed, 0.01f, 0.0f, 100.0f, "%.2f" );
            U::ImGuiUtilities::EndPropertyRow();
        };
        return e;
    }

    // Projectile: integrated by ProjectileSystem in Play. Everything here is authored data except Owner,
    // which the firing script stamps at spawn — shown read-only so a stray hit can be traced back.
    static ComponentEditorEntry MakeProjectileEntry()
    {
        using C = ::Desert::ECS::ProjectileComponent;
        ComponentEditorEntry e;
        e.Name      = "Projectile";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& )
        {
            namespace U = ::Desert::Editor::Utils;
            auto& c     = en.GetComponent<C>();

            U::ImGuiUtilities::ResetPropertyRows();

            U::ImGuiUtilities::BeginPropertyRow( "Velocity", "World units per second" );
            U::ImGuiUtilities::VectorField( "projvel", &c.Velocity.x, 3, 1.0f, "%.0f" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Gravity Scale", "0 = straight line, 1 = full gravity (arc)" );
            ImGui::SliderFloat( "##projgrav", &c.GravityScale, 0.0f, 2.0f, "%.2f" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Life Remaining", "Seconds before it despawns on its own" );
            ImGui::DragFloat( "##projlife", &c.LifeRemaining, 0.1f, 0.0f, 600.0f, "%.1f s" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Damage" );
            ImGui::DragFloat( "##projdmg", &c.Damage, 0.5f, 0.0f, 10000.0f, "%.1f" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Owner", "The shooter, stamped by the script that fired it "
                                                          "(self-hits are skipped)" );
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(
                 "%s", c.Owner.IsNull() ? "None" : std::to_string( static_cast<uint64_t>( c.Owner ) ).c_str() );
            U::ImGuiUtilities::EndPropertyRow();
        };
        return e;
    }

    // Foliage type: the scatter parameters the paint brush reads. They lived ONLY in the viewport's paint
    // overlay, so a type could not be tuned without holding the brush.
    static ComponentEditorEntry MakeFoliageEntry()
    {
        using C = ::Desert::ECS::FoliageComponent;
        ComponentEditorEntry e;
        e.Name      = "Foliage Type";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& )
        {
            namespace U = ::Desert::Editor::Utils;
            auto& f     = en.GetComponent<C>();

            U::ImGuiUtilities::ResetPropertyRows();

            U::ImGuiUtilities::BeginPropertyRow( "Density", "Instances scattered per paint dab" );
            ImGui::SliderFloat( "##foldensity", &f.Density, 1.0f, 80.0f, "%.0f / dab" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Scale Range", "Random uniform scale per instance" );
            ImGui::DragFloatRange2( "##folscale", &f.ScaleMin, &f.ScaleMax, 0.01f, 0.02f, 10.0f, "%.2f", "%.2f" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Z Offset", "Sink (-) / raise (+) along world up" );
            ImGui::DragFloatRange2( "##folz", &f.ZOffsetMin, &f.ZOffsetMax, 0.5f, -500.0f, 500.0f, "%.0f",
                                    "%.0f" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Max Pitch", "Random tilt off the up/normal axis" );
            ImGui::SliderFloat( "##folpitch", &f.MaxPitchDeg, 0.0f, 90.0f, "%.0f deg" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Slope Range", "Only paint where the surface slope fits" );
            ImGui::DragFloatRange2( "##folslope", &f.SlopeMinDeg, &f.SlopeMaxDeg, 0.5f, 0.0f, 90.0f, "%.0f",
                                    "%.0f deg" );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Align to Normal" );
            ImGui::Checkbox( "##folalign", &f.AlignToNormal );
            U::ImGuiUtilities::EndPropertyRow();

            U::ImGuiUtilities::BeginPropertyRow( "Random Yaw" );
            ImGui::Checkbox( "##folyaw", &f.RandomYaw );
            U::ImGuiUtilities::EndPropertyRow();
        };
        return e;
    }

    // Character controller: the authored capsule (reflected) and, in Play, what the physics step is
    // actually reporting back. "Why does he not jump" is answered by On Ground, which the component has
    // always carried and the panel never showed.
    static ComponentEditorEntry MakeCharacterControllerEntry()
    {
        using C = ::Desert::ECS::CharacterControllerComponent;
        ComponentEditorEntry e;
        e.Name              = "Character Controller";
        e.CanRemove         = true;
        e.ReflectedTypeName = "CharacterControllerData";
        e.Has               = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<C>(); };
        e.Add               = []( ::Desert::ECS::Entity& en ) { en.AddComponent<C>(); };
        e.Remove            = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<C>(); };
        e.DataPtr           = []( ::Desert::ECS::Entity& en ) -> void* { return &en.GetComponent<C>().Data; };
        e.Draw = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& ctx )
        {
            namespace U = ::Desert::Editor::Utils;
            auto& c     = en.GetComponent<C>();

            PropertyEditorBuilder::Draw( &c.Data, "CharacterControllerData", ctx.AssetMgr(), ctx.UIHelper,
                                         ctx.FieldFilter );
            if ( ctx.FieldFilter )
                return;

            // Only meaningful while the physics step is running — outside Play these are the last values
            // from the previous run, which would read as live state.
            const bool running = c.RuntimeCharacter != ::Desert::Physics::kInvalidCharacter;

            ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );
            if ( !U::ImGuiUtilities::SectionHeader( ICON_MDI_PULSE "  Runtime", false ) )
                return;

            U::ImGuiUtilities::ResetPropertyRows();
            if ( !running )
            {
                ImGui::TextDisabled( "Live while playing." );
                return;
            }

            const auto readOnlyRow = []( const char* label, const std::string& value )
            {
                U::ImGuiUtilities::BeginPropertyRow( label );
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted( value.c_str() );
                U::ImGuiUtilities::EndPropertyRow();
            };

            char buf[64];
            readOnlyRow( "On Ground", c.OnGround ? "Yes" : "No" );
            std::snprintf( buf, sizeof( buf ), "%.0f cm/s", c.CurrentSpeed );
            readOnlyRow( "Planar Speed", buf );
            std::snprintf( buf, sizeof( buf ), "%.0f cm/s", c.VerticalVelocity );
            readOnlyRow( "Vertical Velocity", buf );
            readOnlyRow( "Swimming", c.Swimming ? "Yes" : "No" );
            std::snprintf( buf, sizeof( buf ), "%.2f, %.2f", c.MoveInput.x, c.MoveInput.y );
            readOnlyRow( "Move Input", buf );
        };
        return e;
    }

    // Blendshape / morph-target editor: one slider per morph target of the entity's mesh (static or skinned).
    // Names + count come from the mesh asset; the sliders write MorphComponent::Weights (index-aligned).
    static ComponentEditorEntry MakeMorphEntry()
    {
        using MC = ::Desert::ECS::MorphComponent;
        ComponentEditorEntry e;
        e.Name      = "Morph Targets";
        e.CanRemove = true;
        e.Has       = []( ::Desert::ECS::Entity& en ) { return en.HasComponent<MC>(); };
        e.Add       = []( ::Desert::ECS::Entity& en ) { en.AddComponent<MC>(); };
        e.Remove    = []( ::Desert::ECS::Entity& en ) { en.RemoveComponent<MC>(); };
        e.Draw      = []( ::Desert::ECS::Entity& en, ::Desert::Core::Scene*, const ComponentEditContext& )
        {
            namespace ImGui = ::ImGui;
            auto& mc        = en.GetComponent<MC>();

            ::Desert::Assets::AssetHandle meshHandle;
            if ( en.HasComponent<::Desert::ECS::SkinnedMeshComponent>() )
                meshHandle = en.GetComponent<::Desert::ECS::SkinnedMeshComponent>().MeshHandle;
            else if ( en.HasComponent<::Desert::ECS::StaticMeshComponent>() )
                meshHandle = en.GetComponent<::Desert::ECS::StaticMeshComponent>().MeshHandle;

            const ::Desert::Assets::MeshAsset* meshAsset =
                 meshHandle ? ::Desert::Runtime::ResourceRegistry::GetMeshService()->GetAsset( meshHandle )
                            : nullptr;

            if ( !meshAsset || meshAsset->GetMorphTargets().empty() )
            {
                ImGui::TextDisabled( "This entity's mesh has no blendshapes (morph targets)." );
                return;
            }

            const auto& targets = meshAsset->GetMorphTargets();

            // Keep the component's arrays in step with the mesh's targets (the mesh may have been swapped).
            if ( mc.Weights.size() != targets.size() )
                mc.Weights.resize( targets.size(), 0.0f );
            mc.TargetNames.resize( targets.size() );
            for ( size_t i = 0; i < targets.size(); ++i )
                mc.TargetNames[i] = targets[i].Name;

            ImGui::TextDisabled( "%d blendshape(s)", static_cast<int>( targets.size() ) );
            for ( size_t i = 0; i < targets.size(); ++i )
            {
                ImGui::PushID( static_cast<int>( i ) );
                const char* name = mc.TargetNames[i].empty() ? "<unnamed>" : mc.TargetNames[i].c_str();
                ImGui::SliderFloat( name, &mc.Weights[i], 0.0f, 1.0f );
                ImGui::PopID();
            }
            if ( ImGui::Button( "Reset All", ImVec2( -1.0f, 0.0f ) ) )
                std::fill( mc.Weights.begin(), mc.Weights.end(), 0.0f );
        };
        return e;
    }
} // namespace Desert::Editor

namespace
{
    const int _desert_emitter_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeEmitterEntry() );

    const int _desert_dirlight_component_reg = ::Desert::Editor::ComponentWidgetRegistry::Get().Register(
         ::Desert::Editor::MakeDirectionalLightEntry() );
    const int _desert_camera_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeCameraEntry() );

    const int _desert_collider_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeColliderEntry() );
    const int _desert_terrain_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeTerrainEntry() );

    const int _desert_ism_component_reg = ::Desert::Editor::ComponentWidgetRegistry::Get().Register(
         ::Desert::Editor::MakeInstancedStaticMeshEntry() );

    const int _desert_morph_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeMorphEntry() );

    const int _desert_charctrl_component_reg = ::Desert::Editor::ComponentWidgetRegistry::Get().Register(
         ::Desert::Editor::MakeCharacterControllerEntry() );
    const int _desert_socket_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeSocketEntry() );
    const int _desert_locomotion_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeLocomotionEntry() );
    const int _desert_projectile_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeProjectileEntry() );
    const int _desert_foliage_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeFoliageEntry() );

    const int _desert_uilayout_component_reg =
         ::Desert::Editor::ComponentWidgetRegistry::Get().Register( ::Desert::Editor::MakeUILayoutEntry() );
} // namespace

// SINGLE SOURCE OF TRUTH: for MESH entities, materials (shader + params) are authored ONLY in
// the material slots (PBR Materials -> Shader picker inside each material). The old standalone
// "Shader Override" editor is gone; MaterialComponent remains (a) the RUNTIME override channel
// for scripts / legacy scenes — surfaced by the PBR Materials banner with one-click clear —
// and (b) the TERRAIN material holder (terrain has no mesh slots), edited below.
// Script component: an entity can run SEVERAL scripts (like UE ActorComponents), shown as a list of slots.
// Per slot: pick the .lua from a dropdown OR drag one from the File Explorer, Reload (hot-reload), and edit
// the script's exposed Properties. "+ Add Script" appends a slot; the X removes one.
DESERT_REGISTER_CUSTOM_COMPONENT(
     ::Desert::ECS::ScriptComponent, "Script", true,
     (
          []( ::Desert::ECS::Entity& e, ::Desert::Core::Scene*, const ::Desert::Editor::ComponentEditContext& )
          {
              namespace fs = std::filesystem;
              auto& sc     = e.GetComponent<::Desert::ECS::ScriptComponent>();

              int removeIndex = -1;
              for ( size_t i = 0; i < sc.Scripts.size(); ++i )
              {
                  ImGui::PushID( static_cast<int>( i ) );
                  auto& slot = sc.Scripts[i];

                  const std::string preview = slot.ScriptPath.empty()
                                                   ? "Select script..."
                                                   : fs::path( slot.ScriptPath ).filename().string();

                  ImGui::SetNextItemWidth( -60.0f ); // leave room for the remove button
                  if ( ImGui::BeginCombo( "##ScriptSel", preview.c_str() ) )
                  {
                      std::error_code ec;
                      if ( fs::exists( "Resources", ec ) )
                      {
                          for ( const auto& it : fs::recursive_directory_iterator( "Resources", ec ) )
                          {
                              if ( !it.is_regular_file() || it.path().extension() != ".lua" )
                                  continue;
                              const std::string rel = it.path().generic_string();
                              if ( ImGui::Selectable( it.path().filename().string().c_str(),
                                                      slot.ScriptPath == rel ) )
                              {
                                  slot.ScriptPath = rel;
                                  slot.Started    = false;
                                  slot.Properties.clear(); // re-seed from the new script's schema below
                              }
                          }
                      }
                      ImGui::EndCombo();
                  }

                  // Drag a .lua from the File Explorer onto the combo.
                  if ( ImGui::BeginDragDropTarget() )
                  {
                      if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( "AssetFile" ) )
                      {
                          const std::string dropped( static_cast<const char*>( payload->Data ) );
                          if ( dropped.size() > 4 && dropped.substr( dropped.size() - 4 ) == ".lua" )
                          {
                              slot.ScriptPath = fs::path( dropped ).generic_string();
                              slot.Started    = false;
                              slot.Properties.clear();
                          }
                      }
                      ImGui::EndDragDropTarget();
                  }

                  ImGui::SameLine();
                  if ( ImGui::Button( "X" ) )
                      removeIndex = static_cast<int>( i );

                  if ( !slot.ScriptPath.empty() )
                  {
                      ImGui::TextDisabled( "%s", slot.ScriptPath.c_str() );

                      if ( ImGui::Button( "Reload" ) )
                          slot.Started = false; // re-read on the next Play frame (hot-reload)

                      // ---- Exposed properties (the script's `Properties` table) ----
                      if ( slot.Properties.empty() )
                          slot.Properties = ::Desert::Scripting::ReadScriptProperties( slot.ScriptPath );

                      ImGui::SameLine();
                      if ( ImGui::Button( "Refresh Props" ) )
                      {
                          // Re-read the schema, keeping existing values for properties that still exist.
                          auto schema = ::Desert::Scripting::ReadScriptProperties( slot.ScriptPath );
                          for ( auto& s : schema )
                          {
                              auto old = std::find_if( slot.Properties.begin(), slot.Properties.end(),
                                                       [&]( const auto& p )
                                                       { return p.Name == s.Name && p.Type == s.Type; } );
                              if ( old != slot.Properties.end() )
                                  s = *old;
                          }
                          slot.Properties = std::move( schema );
                      }

                      for ( auto& p : slot.Properties )
                      {
                          switch ( p.Type )
                          {
                              case ::Desert::Scripting::PropertyType::Number:
                              {
                                  float v = static_cast<float>( p.Number );
                                  if ( ImGui::DragFloat( p.Name.c_str(), &v, 0.01f ) )
                                      p.Number = v;
                                  break;
                              }
                              case ::Desert::Scripting::PropertyType::Bool:
                                  ImGui::Checkbox( p.Name.c_str(), &p.Bool );
                                  break;
                              case ::Desert::Scripting::PropertyType::String:
                              {
                                  char buf[256] = { 0 };
                                  std::strncpy( buf, p.Str.c_str(), sizeof( buf ) - 1 );
                                  if ( ImGui::InputText( p.Name.c_str(), buf, sizeof( buf ) ) )
                                      p.Str = buf;
                                  break;
                              }
                          }
                      }
                  }

                  ImGui::Separator();
                  ImGui::PopID();
              }

              if ( removeIndex >= 0 )
                  sc.Scripts.erase( sc.Scripts.begin() + removeIndex );

              if ( ImGui::Button( "+ Add Script" ) )
                  sc.Scripts.emplace_back();
          } ) )

// Text (SDF world-space label). Simple field editor; the mesh rebuilds automatically when Text/
// Font/Size change (TextECSSystem compares against its Built* cache).
DESERT_REGISTER_CUSTOM_COMPONENT(
     ::Desert::ECS::TextComponent, "Text", true,
     (
          []( ::Desert::ECS::Entity& e, ::Desert::Core::Scene*, const ::Desert::Editor::ComponentEditContext& )
          {
              auto& tc = e.GetComponent<::Desert::ECS::TextComponent>();

              ::Desert::Editor::Utils::ImGuiUtilities::ResetPropertyRows();

              char buf[512] = { 0 };
              std::strncpy( buf, tc.Text.c_str(), sizeof( buf ) - 1 );
              ::Desert::Editor::Utils::ImGuiUtilities::BeginPropertyRow( "Text", nullptr, 60.0f );
              if ( ImGui::InputTextMultiline( "##text", buf, sizeof( buf ), ImVec2( -1.0f, 56.0f ) ) )
                  tc.Text = buf;
              ::Desert::Editor::Utils::ImGuiUtilities::EndPropertyRow();

              // Font: an ASSET HANDLE (never a raw path) — pick one of the preloaded fonts from the dropdown
              // or drag a .ttf from the Content Browser. FontService owns the handle<->path registry and the
              // preloaded set; "Default" (null handle) falls back to the engine's built-in font.
              auto*             fs      = ::Desert::Runtime::ResourceRegistry::GetFontService();
              const uint64_t    curHnd  = static_cast<uint64_t>( tc.Font );
              const std::string curPath = fs ? fs->PathForHandle( curHnd ) : "";
              const std::string preview =
                   curHnd == 0
                        ? "Default"
                        : ( curPath.empty() ? "(missing)" : std::filesystem::path( curPath ).stem().string() );
              ::Desert::Editor::Utils::ImGuiUtilities::BeginPropertyRow( "Font" );
              if ( ImGui::BeginCombo( "##textfont", preview.c_str() ) )
              {
                  if ( ImGui::Selectable( "Default", curHnd == 0 ) )
                      tc.Font = ::Desert::Assets::AssetHandle();
                  if ( fs )
                  {
                      for ( const auto& f : fs->AvailableFonts() )
                      {
                          const uint64_t h   = fs->RegisterFont( f );
                          const bool     sel = ( h == curHnd );
                          if ( ImGui::Selectable( std::filesystem::path( f ).stem().string().c_str(), sel ) )
                              tc.Font = ::Desert::Assets::AssetHandle( h );
                          if ( sel )
                              ImGui::SetItemDefaultFocus();
                      }
                  }
                  ImGui::EndCombo();
              }
              ::Desert::Editor::Utils::ImGuiUtilities::EndPropertyRow();
              if ( ImGui::BeginDragDropTarget() )
              {
                  if ( const ImGuiPayload* pl =
                            ImGui::AcceptDragDropPayload( ::Desert::Editor::DragPayloads::FontFile ) )
                  {
                      const std::string path( static_cast<const char*>( pl->Data ),
                                              pl->DataSize > 0 ? pl->DataSize - 1 : 0 );
                      if ( fs && !path.empty() )
                          tc.Font = ::Desert::Assets::AssetHandle( fs->RegisterFont( path ) );
                  }
                  ImGui::EndDragDropTarget();
              }
              if ( ImGui::IsItemHovered() )
                  ImGui::SetTooltip( "Pick a preloaded font or drag a .ttf here from the Content Browser" );

              namespace TU = ::Desert::Editor::Utils;

              TU::ImGuiUtilities::BeginPropertyRow( "Color" );
              ImGui::ColorEdit4( "##textcolor", &tc.Color.x );
              TU::ImGuiUtilities::EndPropertyRow();

              TU::ImGuiUtilities::BeginPropertyRow( "Size", "World units per em" );
              ImGui::DragFloat( "##textsize", &tc.Size, 1.0f, 1.0f, 10000.0f, "%.1f cm" );
              TU::ImGuiUtilities::EndPropertyRow();

              TU::ImGuiUtilities::BeginPropertyRow( "Emissive Intensity", "Above ~1 the text blooms" );
              ImGui::DragFloat( "##textemissive", &tc.EmissiveIntensity, 0.05f, 0.0f, 20.0f, "%.2f" );
              TU::ImGuiUtilities::EndPropertyRow();
              if ( ImGui::IsItemHovered() )
                  ImGui::SetTooltip( "> ~1 makes the text bloom (it renders into the HDR scene)" );
              TU::ImGuiUtilities::BeginPropertyRow( "Billboard", "Always face the camera" );
              ImGui::Checkbox( "##textbillboard", &tc.Billboard );
              TU::ImGuiUtilities::EndPropertyRow();
          } ) )
