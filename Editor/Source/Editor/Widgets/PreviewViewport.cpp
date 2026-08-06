#include "PreviewViewport.hpp"

#include "UIHelper/ImGuiUI.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/ECS/System/MeshECSSystem.hpp>
#include <Engine/ECS/System/SkyboxECSSystem.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <algorithm>
#include <cmath>

namespace Desert::Editor
{
    // A Desert::ImGui also exists (the engine's own UI namespace), so unqualified ImGui:: would resolve
    // there in this TU. Same alias the other editor panels use.
    namespace ImGui = ::ImGui;

    namespace
    {
        constexpr float kFov        = 35.0f; // degrees; a longer lens than the viewport's = less distortion
        constexpr float kNearPlane  = 1.0f;  // centimetres (see Common/Core/Units.hpp)
        constexpr float kFarPlane   = 100000.0f;
        constexpr float kPitchLimit = 1.45f; // just shy of straight down/up, so the orbit never gimbals
        constexpr float kFitMargin  = 1.25f; // leave a little air around the framed bounds

        // Sun/key-light travel direction (DirectionLight stores where the light GOES, the shader lights
        // along -Direction). Same warm key as the asset thumbnails, from above and to the side.
        constexpr glm::vec3 kLightTravel{ 2.0f, -6.0f, 5.0f };

        float RadiusOfPrimitive( PreviewViewport::Shape shape )
        {
            // Primitives are generated one metre = 100 units across (PrimitiveMeshFactory::kPrimitiveSize).
            constexpr float kHalf = 50.0f;
            switch ( shape )
            {
                case PreviewViewport::Shape::Cube:
                    return kHalf * 1.732f; // corner-to-centre
                case PreviewViewport::Shape::Plane:
                    return kHalf * 1.415f;
                case PreviewViewport::Shape::Sphere:
                default:
                    return kHalf;
            }
        }

        Geometry::PrimitiveType ToPrimitive( PreviewViewport::Shape shape )
        {
            switch ( shape )
            {
                case PreviewViewport::Shape::Cube:
                    return Geometry::PrimitiveType::Cube;
                case PreviewViewport::Shape::Plane:
                    return Geometry::PrimitiveType::Plane;
                case PreviewViewport::Shape::Sphere:
                default:
                    return Geometry::PrimitiveType::Sphere;
            }
        }
    } // namespace

    PreviewViewport::~PreviewViewport()
    {
        if ( !m_Inited )
            return;

        // Closing a scene view (or quitting) can destroy this while the last submitted frame is still
        // executing against our pipelines and descriptor pools. Idle, then release the scene before the
        // renderer that owns its passes.
        Graphic::Renderer::GetInstance().WaitDeviceIdle();
        m_Scene.reset();
        m_Renderer.reset();
    }

    void PreviewViewport::EnsureInit()
    {
        if ( m_Inited )
            return;

        m_Renderer = std::make_unique<Graphic::SceneRenderer>();
        m_Scene    = std::make_shared<::Desert::Core::Scene>( "DetailsPreview", m_Renderer.get() );
        m_Scene->Init();

        // Clean preview: no editor ground grid, no shadows or bloom to muddy a small image. FXAA keeps the
        // silhouette smooth at inspector sizes (there is no supersampling here — this renders live).
        auto& settings         = m_Scene->GetSettings();
        settings.ShowGrid      = false;
        settings.EnableShadows = false;
        settings.EnableBloom   = false;
        settings.AA            = ::Desert::Core::AntiAliasingMode::FXAA;

        // The selection outline is pushed by the editor loop every frame; this renderer is never fed by it,
        // so disable it explicitly or a stale outline could bleed into the preview.
        m_Renderer->SetOutlineSettings( glm::vec3( 0.0f ), 0.0f, 0.0f, false );

        // OUR OWN camera, unlike AssetThumbnailRenderer: a scene's auto-created main camera is the
        // input-driven EditorCamera (it would fight the real viewport for the mouse and ignores transforms),
        // so the thumbnail renderer has to frame by scaling the object. A GameplayCamera we drive by hand
        // from the orbit state gives real orbit + zoom and leaves the target's transform at identity.
        m_Camera = std::make_shared<::Desert::Core::GameplayCamera>();
        m_Scene->SetActiveCamera( m_Camera );

        auto  light           = m_Scene->CreateNewEntity( "PreviewLight" );
        auto& lightC          = light.AddComponent<ECS::DirectionLightComponent>();
        lightC.Data.Intensity = 3.5f;
        lightC.Data.Color     = { 1.0f, 0.97f, 0.92f };
        light.GetComponent<ECS::TransformComponent>().Translation = kLightTravel;

        m_Target = m_Scene->CreateNewEntity( "PreviewTarget" );
        m_Target.AddComponent<ECS::StaticMeshComponent>();

        m_Scene->AddSystem<ECS::MeshECSSystem>();
        m_Scene->AddSystem<ECS::SkyboxECSSystem>();

        // Procedural sky as the backdrop. Unlike the thumbnails (fixed camera looking down, so only the
        // ground hemisphere showed) this camera can point anywhere, so the whole dome is kept cohesive and
        // fairly dark — a neutral studio backdrop that doesn't compete with the asset.
        auto  skyEnt        = m_Scene->CreateNewEntity( "PreviewSky" );
        auto& skyC          = skyEnt.AddComponent<ECS::SkyboxComponent>();
        skyC.Procedural     = true;
        skyC.ZenithColor    = { 0.10f, 0.13f, 0.19f };
        skyC.HorizonColor   = { 0.22f, 0.25f, 0.31f };
        skyC.GroundColor    = { 0.13f, 0.14f, 0.17f };
        skyC.SunColor       = { 1.00f, 0.95f, 0.85f };
        skyC.SkyBrightness  = 1.0f;
        skyC.HorizonFalloff = 0.6f;
        skyC.SunGlow        = 0.5f;
        skyC.StarIntensity  = 0.0f;
        skyC.SunIntensity   = 10.0f;
        skyC.SunDiskRadius  = 0.02f;
        skyC.RequestBake    = true;

        // Same values through the direct call so the sky is enabled from frame 0 (the ECS command path alone
        // proved insufficient in a minimal scene — see AssetThumbnailRenderer).
        Graphic::SkySettings sky;
        sky.ZenithColor    = skyC.ZenithColor;
        sky.HorizonColor   = skyC.HorizonColor;
        sky.GroundColor    = skyC.GroundColor;
        sky.SunColor       = skyC.SunColor;
        sky.SkyBrightness  = skyC.SkyBrightness;
        sky.HorizonFalloff = skyC.HorizonFalloff;
        sky.SunGlow        = skyC.SunGlow;
        sky.StarIntensity  = 0.0f;
        m_Renderer->SetProceduralSky( true, -glm::normalize( kLightTravel ), skyC.SunIntensity, skyC.SunDiskRadius,
                                      true, Graphic::CloudSettings{}, sky );

        m_Inited = true;
    }

    void PreviewViewport::SetMesh( const Assets::AssetHandle&              mesh,
                                   const std::vector<Assets::AssetHandle>& materials )
    {
        if ( static_cast<uint64_t>( mesh ) == 0 )
        {
            Clear();
            return;
        }

        EnsureInit();

        auto& smc = m_Target.GetComponent<ECS::StaticMeshComponent>();
        smc.RuntimeMesh.reset();
        smc.Primitive.reset();
        smc.RuntimeMaterialInstances.clear();
        smc.MeshHandle    = mesh;
        smc.MaterialSlots = materials;

        // Upright, like the material preview sets it: the target entity is reused across previews, so a
        // rotation left by an earlier one would tilt this mesh for no reason.
        m_Target.GetComponent<ECS::TransformComponent>().Rotation = glm::vec3( 0.0f );

        m_MeshHandle  = mesh;
        m_HasContent  = true;
        m_Focus       = glm::vec3( 0.0f );
        m_FrameRadius = 100.0f; // stand-in until the bounds are known (see TryFrameMesh)
        ResetView();
        m_Framed = TryFrameMesh();
    }

    bool PreviewViewport::TryFrameMesh()
    {
        auto* meshAsset = Runtime::ResourceRegistry::GetMeshService()->Get( m_MeshHandle );
        if ( !meshAsset )
            return false;

        // Union the submesh AABBs in MESH space, applying each submesh transform to its 8 corners — meshes
        // that keep their offset in a submesh transform frame wrongly otherwise (huge / off-screen).
        glm::vec3 mn( 1e9f ), mx( -1e9f );
        for ( const auto& sm : meshAsset->GetSubmeshes() )
        {
            const glm::vec3 lo = sm.BoundingBox.Min, hi = sm.BoundingBox.Max;
            for ( int corner = 0; corner < 8; ++corner )
            {
                const glm::vec3 p( ( corner & 1 ) ? hi.x : lo.x, ( corner & 2 ) ? hi.y : lo.y,
                                   ( corner & 4 ) ? hi.z : lo.z );
                const glm::vec3 w = glm::vec3( sm.Transform * glm::vec4( p, 1.0f ) );
                mn                = glm::min( mn, w );
                mx                = glm::max( mx, w );
            }
        }
        if ( mx.x < mn.x )
            return false; // no submeshes yet

        m_Focus = ( mn + mx ) * 0.5f;

        // Half the LARGEST EXTENT, not half the diagonal — the same convention RadiusOfPrimitive uses for
        // the material preview (a 100-unit cube gives 50, not 87). Framing by the diagonal pushes the
        // camera ~1.7x further back for the same object, which is why a mesh sat small and far away while
        // a material sphere filled its box.
        const glm::vec3 extent = mx - mn;
        m_FrameRadius          = std::max( glm::max( extent.x, glm::max( extent.y, extent.z ) ) * 0.5f, 1.0f );

        ResetView();
        return true;
    }

    void PreviewViewport::SetMaterial( const Assets::AssetHandle& material, Shape shape )
    {
        EnsureInit();

        auto& smc      = m_Target.GetComponent<ECS::StaticMeshComponent>();
        smc.MeshHandle = Assets::AssetHandle( static_cast<uint64_t>( 0 ) );
        // Clearing the runtime instances forces a rebuild against the new handle; dropping the runtime mesh
        // makes a shape change rebuild the geometry.
        smc.RuntimeMaterialInstances.clear();
        smc.RuntimeMesh.reset();
        smc.Primitive     = ToPrimitive( shape );
        smc.MaterialSlots = { material };

        // A flat card must face the camera to be readable (a grass atlas garbles on a sphere), so the plane
        // preview keeps a fixed front-on view instead of an orbit start angle.
        auto& tc    = m_Target.GetComponent<ECS::TransformComponent>();
        tc.Rotation = glm::vec3( 0.0f );

        m_MeshHandle  = Assets::AssetHandle( static_cast<uint64_t>( 0 ) );
        m_Framed      = true; // a primitive's size is known up front
        m_Focus       = glm::vec3( 0.0f );
        m_FrameRadius = RadiusOfPrimitive( shape );
        m_HasContent  = true;
        ResetView();
    }

    void PreviewViewport::Clear()
    {
        m_HasContent = false;
        m_MeshHandle = Assets::AssetHandle( static_cast<uint64_t>( 0 ) );
        m_Framed     = false;
        if ( !m_Inited )
            return;

        auto& smc      = m_Target.GetComponent<ECS::StaticMeshComponent>();
        smc.MeshHandle = Assets::AssetHandle( static_cast<uint64_t>( 0 ) );
        smc.Primitive.reset();
        smc.MaterialSlots.clear();
        smc.RuntimeMaterialInstances.clear();
        smc.RuntimeMesh.reset();
    }

    void PreviewViewport::ResetView()
    {
        // Distance that fits the bounding sphere in the vertical FOV, with a margin.
        const float halfFov = glm::radians( kFov ) * 0.5f;
        m_Distance          = m_FrameRadius / std::sin( halfFov ) * kFitMargin;
        m_Yaw               = -0.6f;
        m_Pitch             = 0.4f;
    }

    void PreviewViewport::ApplyCamera( uint32_t width, uint32_t height )
    {
        // Spherical orbit around m_Focus. Pitch is the camera's ELEVATION (positive = above the target); the
        // Euler the camera wants is its own pitch, which is the opposite sign.
        const float     cp = std::cos( m_Pitch );
        const glm::vec3 offset{ cp * std::sin( m_Yaw ), std::sin( m_Pitch ), cp * std::cos( m_Yaw ) };
        const glm::vec3 position = m_Focus + offset * m_Distance;

        m_Camera->SetFromTransform( position, glm::vec3( -m_Pitch, m_Yaw, 0.0f ), kFov, kNearPlane, kFarPlane,
                                    width, height );
    }

    void PreviewViewport::Update( uint32_t width, uint32_t height )
    {
        if ( !m_HasContent || width == 0 || height == 0 )
            return;

        EnsureInit();

        // A mesh requested before the MeshService had it: keep trying so it gets framed the frame it lands.
        if ( !m_Framed && static_cast<uint64_t>( m_MeshHandle ) != 0 )
            m_Framed = TryFrameMesh();

        // Resize recreates framebuffers and idles the GPU, so only on an actual change.
        if ( width != m_Width || height != m_Height )
        {
            m_Scene->Resize( width, height );
            m_Width  = width;
            m_Height = height;
        }

        ApplyCamera( width, height );

        // Recorded into the editor's current frame command buffer, submitted when the frame ends. This is
        // why Update() must run from OnPreUpdate() and never from OnUIRender().
        m_Scene->BeginScene();
        m_Scene->OnUpdate( Common::Timestep( 0.016f ) );
        m_Scene->EndScene();
    }

    bool PreviewViewport::Draw( UI::UIHelper& uiHelper, const ImVec2& size )
    {
        const ImVec2 drawSize( std::max( size.x, 16.0f ), std::max( size.y, 16.0f ) );
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        // The interactive item comes FIRST and the image is painted into it, because UIHelper::Image is not
        // an ImGui item and so can't be hovered or dragged.
        ImGui::InvisibleButton( "##preview_viewport", drawSize,
                                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight );
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();

        ImDrawList*  dl = ImGui::GetWindowDrawList();
        const ImVec2 end( origin.x + drawSize.x, origin.y + drawSize.y );

        const void* texId = nullptr;
        if ( m_HasContent && m_Width > 0 )
        {
            if ( const auto image = m_Scene->GetFinalImage() )
                texId = uiHelper.GetTextureID( image );
        }

        constexpr float kRounding = 4.0f;
        if ( texId )
        {
            dl->AddImageRounded( reinterpret_cast<ImTextureID>( const_cast<void*>( texId ) ), origin, end,
                                 ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, kRounding );
        }
        else
        {
            dl->AddRectFilled( origin, end, IM_COL32( 28, 30, 34, 255 ), kRounding );
            const char*  label = m_HasContent ? "Rendering..." : "No preview";
            const ImVec2 ts    = ImGui::CalcTextSize( label );
            dl->AddText(
                 ImVec2( origin.x + ( drawSize.x - ts.x ) * 0.5f, origin.y + ( drawSize.y - ts.y ) * 0.5f ),
                 IM_COL32( 130, 135, 145, 255 ), label );
        }
        dl->AddRect( origin, end, ImGui::GetColorU32( ImGuiCol_Border ), kRounding );

        if ( !m_HasContent )
            return false;

        bool interacting = false;

        // LMB-drag orbits, RMB-drag pans — the same split as the main viewport, so the muscle memory
        // carries over. Panning moves the orbit's focus in the camera's own screen plane.
        if ( active )
        {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if ( delta.x != 0.0f || delta.y != 0.0f )
            {
                if ( ImGui::IsMouseDown( ImGuiMouseButton_Right ) )
                {
                    // Screen-proportional: one pixel moves the focus by the same fraction of the framed
                    // object at any zoom, so panning never feels different when you are close in.
                    const float     cp = std::cos( m_Pitch );
                    const glm::vec3 forward{ -cp * std::sin( m_Yaw ), -std::sin( m_Pitch ),
                                             -cp * std::cos( m_Yaw ) };
                    const glm::vec3 right = glm::normalize( glm::cross( forward, glm::vec3( 0, 1, 0 ) ) );
                    const glm::vec3 up    = glm::normalize( glm::cross( right, forward ) );

                    const float speed = m_Distance / std::max( drawSize.y, 1.0f );
                    m_Focus += ( -right * delta.x + up * delta.y ) * speed;
                }
                else
                {
                    constexpr float kOrbitSpeed = 0.008f; // radians per pixel
                    m_Yaw -= delta.x * kOrbitSpeed;
                    m_Pitch = std::clamp( m_Pitch + delta.y * kOrbitSpeed, -kPitchLimit, kPitchLimit );
                }
            }
            interacting = true;
        }

        if ( hovered )
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if ( wheel != 0.0f )
            {
                // Multiplicative so the zoom feels the same at every distance, clamped so the asset can
                // neither be swallowed by the near plane nor lost to a dot.
                m_Distance  = std::clamp( m_Distance * std::exp( -wheel * 0.12f ), m_FrameRadius * 0.6f,
                                          m_FrameRadius * 20.0f );
                interacting = true;
            }
            if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
                ResetView();
                interacting = true;
            }
            ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );
            ImGui::SetTooltip( "Drag to orbit - right-drag to pan - wheel to zoom - double-click to reset" );
        }

        return interacting;
    }
} // namespace Desert::Editor
