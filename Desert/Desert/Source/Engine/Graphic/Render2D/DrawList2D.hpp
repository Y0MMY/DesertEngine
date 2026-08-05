#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// CPU-side retained draw list for the 2D batcher — the analogue of ImGui's ImDrawList, but engine-owned.
// The UI tree (Canvas / UILayout) records primitives here as plain geometry; the GPU backend (Render2D)
// then uploads Vertices/Indices once and issues one indexed draw per DrawCommand. Kept free of any GPU /
// Vulkan / ECS dependency so it is pure and unit-testable (like Desert::UI::UILayout).
namespace Desert::Graphic::Render2D
{
    struct Vertex2D
    {
        glm::vec2 Position; // pixel coordinates (top-left origin)
        glm::vec2 UV;
        glm::vec4 Color; // straight (non-premultiplied) RGBA, 0..1
    };

    // A run of indices sharing the same GPU state (bound texture + clip rect). Consecutive primitives with
    // an identical state extend the current command instead of opening a new one, so flat-colour UI collapses
    // to a single draw. A null Texture means "solid" — the backend binds its 1x1 white texture.
    struct DrawCommand
    {
        const void* Texture     = nullptr;                    // opaque texture id (engine Image2D*), null => white
        glm::vec4   ClipRect    = { 0.0f, 0.0f, 0.0f, 0.0f }; // x,y,w,h px; W<=0 => unclipped
        uint32_t    IndexOffset = 0;                          // first index into GetIndices()
        uint32_t    IndexCount  = 0;                          // number of indices in this batch
        bool        Text        = false;                      // true => SDF glyph atlas (text pipeline), else UI2D

        // GLASS (backdrop blur): this batch samples the blurred scene snapshot instead of a texture, and
        // masks itself with a rounded rectangle. Every glass rect carries its own rect/radius/blur in push
        // constants, so glass commands are never merged with anything — one element, one draw.
        bool      Glass      = false;
        glm::vec4 GlassRect  = { 0.0f, 0.0f, 0.0f, 0.0f }; // min.xy, max.xy in pixels
        float     GlassRound = 0.0f;                       // corner radius, px
        float     GlassLod   = 0.0f;                       // blur level in the backdrop pyramid
    };

    class DrawList2D
    {
    public:
        // Clear geometry for a new frame while keeping the allocated capacity (no per-frame reallocation).
        void Reset();

        // Filled axis-aligned rectangle. `min`/`max` are top-left / bottom-right pixel corners. `rounding` >0
        // rounds the corners (radius px, clamped to half the shorter side) via a triangle fan.
        void AddRectFilled( const glm::vec2& min, const glm::vec2& max, const glm::vec4& color,
                            float rounding = 0.0f );

        // Frosted-glass rectangle: fills with the BLURRED scene behind it, tinted by @p tint (its alpha is
        // how much of the tint covers the blur — 0 = pure blur, 1 = flat colour). @p blur01 picks how strong
        // the blur is (0..1, mapped to the backdrop pyramid's LODs by the backend). Rounded by @p rounding,
        // antialiased in the shader rather than tessellated. Falls back to a plain rounded rect when the
        // backend has no backdrop image (e.g. the very first frame).
        void AddGlassRect( const glm::vec2& min, const glm::vec2& max, const glm::vec4& tint,
                           float rounding = 0.0f, float blur01 = 1.0f );

        // Vertical two-colour gradient fill (top -> bottom). Solid batch (white texture).
        void AddRectFilledMultiColor( const glm::vec2& min, const glm::vec2& max, const glm::vec4& topColor,
                                      const glm::vec4& bottomColor );

        // Rectangle outline of the given pixel `thickness`, drawn as four filled bars (sharp corners).
        void AddRect( const glm::vec2& min, const glm::vec2& max, const glm::vec4& color, float thickness );

        // Filled triangle (e.g. a dropdown arrow). Solid batch (white texture).
        void AddTriangleFilled( const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2,
                                const glm::vec4& color );

        // Straight line segment of pixel `thickness`, drawn as a quad (butt caps). Solid batch. Used by the
        // built-in vector icon set (checks, chevrons, strokes).
        void AddLine( const glm::vec2& a, const glm::vec2& b, const glm::vec4& color, float thickness );

        // Annulus (ring) centred at `center`, from `innerRadius` to `outerRadius` (px), as a triangle strip.
        // The colour sweeps `colorA` -> `colorB` -> `colorA` around the ring (smooth, seamless), giving a
        // conic-style gradient border for circular avatars / status rings / progress rings. Solid batch.
        void AddRing( const glm::vec2& center, float outerRadius, float innerRadius, const glm::vec4& colorA,
                      const glm::vec4& colorB, int segments = 48 );

        // Clip subsequently-added primitives to `min`..`max` (px), intersected with the current clip (so
        // nested masks compose). Pair with PopClipRect. The backend applies it as a scissor per batch.
        void PushClipRect( const glm::vec2& min, const glm::vec2& max );
        void PopClipRect();

        // Textured axis-aligned quad. `texture` is an opaque id (engine Image2D*) the backend binds; `uv0`/
        // `uv1` are the top-left / bottom-right texture coordinates (0..1), `tint` multiplies the sampled
        // texel (white = unchanged). Same-texture quads batch together; a new texture opens a new command.
        void AddImage( const void* texture, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv0,
                       const glm::vec2& uv1, const glm::vec4& tint );

        // Textured quad sampled as an SDF glyph (the backend routes these to the text pipeline). Same args as
        // AddImage; `atlas` is the font's SDF atlas id, `uv0`/`uv1` the glyph's atlas sub-rect, `color` the
        // text colour. Text quads batch separately from image/solid quads even on the same texture.
        void AddText( const void* atlas, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv0,
                      const glm::vec2& uv1, const glm::vec4& color );

        const std::vector<Vertex2D>& GetVertices() const
        {
            return m_Vertices;
        }
        const std::vector<uint32_t>& GetIndices() const
        {
            return m_Indices;
        }
        const std::vector<DrawCommand>& GetCommands() const
        {
            return m_Commands;
        }

        bool Empty() const
        {
            return m_Indices.empty();
        }

    private:
        // Returns a command matching the given state (texture + text mode), extending the last one when
        // possible or opening a new one anchored at the current end of the index buffer.
        DrawCommand& CurrentCommand( const void* texture, bool text );

        // Append one textured/tinted quad (the shared path behind AddRectFilled / AddImage / AddText).
        void AddQuad( const void* texture, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv0,
                      const glm::vec2& uv1, const glm::vec4& color, bool text );

        std::vector<Vertex2D>    m_Vertices;
        std::vector<uint32_t>    m_Indices;
        std::vector<DrawCommand> m_Commands;

        glm::vec4              m_CurrentClip = { 0.0f, 0.0f, 0.0f, 0.0f }; // x,y,w,h px; W<=0 => unclipped
        std::vector<glm::vec4> m_ClipStack;
    };
} // namespace Desert::Graphic::Render2D
