#pragma once

#include <Engine/Assets/Common.hpp>
#include <Engine/Core/Camera.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>

#include <ImGui/imgui.h>

#include <memory>
#include <vector>

namespace Desert::Editor::UI
{
    class UIHelper;
}

namespace Desert::Editor
{
    // A LIVE asset preview: its own tiny scene (target + key light + procedural sky) rendered offscreen
    // every frame and shown as an image you can orbit. The Details panel's answer to "what does this mesh
    // / material actually look like".
    //
    // Not the same job as AssetThumbnailRenderer, which captures a PNG over several frames for the asset
    // browser grid and cannot move its camera (the scene's auto-created main camera is the input-driven
    // EditorCamera, so thumbnails are framed by SCALING the object). This one installs its own
    // GameplayCamera as the scene's active camera and drives it from orbit angles, so it can be turned,
    // zoomed and framed properly.
    //
    // FRAME ORDERING IS PART OF THE CONTRACT: Update() records a scene render and must run from a panel's
    // OnPreUpdate(); Draw() only shows the last image and handles input, from OnUIRender(). Rendering from
    // inside the ImGui pass would destroy descriptor pools whose sets are bound to the recording command
    // buffer — the editor has been bitten by exactly that (see ViewportPanel's deferred resize).
    class PreviewViewport
    {
    public:
        PreviewViewport() = default;
        // Waits for the GPU before releasing the scene: this owns pipelines, framebuffers and descriptor
        // pools that a submitted frame may still be executing against.
        ~PreviewViewport();

        PreviewViewport( const PreviewViewport& )            = delete;
        PreviewViewport& operator=( const PreviewViewport& ) = delete;

        enum class Shape
        {
            Sphere, // material preview default
            Cube,
            Plane // right for cutout / foliage materials — a grass card garbles on a sphere
        };

        // Show a mesh, auto-framed by its bounds. `materials` is applied slot-by-slot (pass the entity's
        // slots so the preview shows the real look); empty = the default material.
        void SetMesh( const Assets::AssetHandle& mesh, const std::vector<Assets::AssetHandle>& materials = {} );

        // Show a material on a primitive.
        void SetMaterial( const Assets::AssetHandle& material, Shape shape = Shape::Sphere );

        void Clear();

        // True once something has been set (and so there is anything to draw).
        [[nodiscard]] bool HasContent() const
        {
            return m_HasContent;
        }

        // Records this frame's offscreen render at the requested size. Call ONCE per frame from
        // OnPreUpdate(), and only while the preview is actually visible — a collapsed section or a
        // scrolled-away row should skip it so an inspector full of assets doesn't render them all.
        void Update( uint32_t width, uint32_t height );

        // Draws the last rendered image and handles interaction: LMB-drag orbits, RMB-drag pans, wheel
        // zooms, double-click re-frames. Returns true while the user is manipulating it.
        bool Draw( UI::UIHelper& uiHelper, const ImVec2& size );

        // Re-frame on the current content's bounds (what double-click does).
        void ResetView();

    private:
        void EnsureInit();
        void ApplyCamera( uint32_t width, uint32_t height );
        // Bounds of the current mesh handle, if the MeshService already has it. False while it is still
        // loading — Update() keeps retrying so a mesh that arrives a few frames later still gets framed
        // instead of being previewed against a guessed radius.
        bool TryFrameMesh();

        std::unique_ptr<Graphic::SceneRenderer> m_Renderer;
        // Fully qualified: a Desert::Editor::Core namespace also exists, so an unqualified Core::Scene
        // would resolve there in TUs that see it.
        std::shared_ptr<::Desert::Core::Scene> m_Scene;
        // Our own camera, driven by the orbit state (not the scene's input-driven EditorCamera).
        std::shared_ptr<::Desert::Core::GameplayCamera> m_Camera;
        ECS::Entity                                     m_Target;
        bool                                            m_Inited     = false;
        bool                                            m_HasContent = false;
        Assets::AssetHandle m_MeshHandle{ static_cast<uint64_t>( 0 ) }; // non-zero while previewing a mesh
        bool                m_Framed = false;                           // bounds resolved -> view is correct

        // Orbit state, persisted per widget instance so a preview keeps its angle across frames (and, since
        // the panel owns the widget, across selections of the same kind).
        float     m_Yaw      = -0.6f; // radians
        float     m_Pitch    = 0.5f;
        float     m_Distance = 3.0f; // world units, derived from the content's bounds on ResetView
        glm::vec3 m_Focus{ 0.0f };
        float     m_FrameRadius = 1.0f;                  // bounding radius of the current content
        glm::vec3 m_FrameHalfExtent{ 0.5f, 0.5f, 0.5f }; // half-size of its box, for the exact fit
        // Round content (the sphere primitive) is bounded by its own radius from every angle, so it fits
        // tighter than its box would. Everything else — a cube, a card, a measured mesh — is fitted by the
        // corners of that box. Using one rule for both leaves the other one small in frame.
        bool m_FrameIsRound = false;

        uint32_t m_Width = 0, m_Height = 0;
    };
} // namespace Desert::Editor
