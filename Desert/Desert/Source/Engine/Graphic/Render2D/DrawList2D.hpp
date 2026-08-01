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
    };

    class DrawList2D
    {
    public:
        // Clear geometry for a new frame while keeping the allocated capacity (no per-frame reallocation).
        void Reset();

        // Filled axis-aligned rectangle. `min`/`max` are top-left / bottom-right pixel corners.
        void AddRectFilled( const glm::vec2& min, const glm::vec2& max, const glm::vec4& color );

        // Textured axis-aligned quad. `texture` is an opaque id (engine Image2D*) the backend binds; `uv0`/
        // `uv1` are the top-left / bottom-right texture coordinates (0..1), `tint` multiplies the sampled
        // texel (white = unchanged). Same-texture quads batch together; a new texture opens a new command.
        void AddImage( const void* texture, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv0,
                       const glm::vec2& uv1, const glm::vec4& tint );

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
        // Returns a command matching the given state, extending the last one when possible or opening a new
        // one anchored at the current end of the index buffer.
        DrawCommand& CurrentCommand( const void* texture );

        // Append one textured/tinted quad (the shared path behind AddRectFilled / AddImage).
        void AddQuad( const void* texture, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv0,
                      const glm::vec2& uv1, const glm::vec4& color );

        std::vector<Vertex2D>    m_Vertices;
        std::vector<uint32_t>    m_Indices;
        std::vector<DrawCommand> m_Commands;
    };
} // namespace Desert::Graphic::Render2D
