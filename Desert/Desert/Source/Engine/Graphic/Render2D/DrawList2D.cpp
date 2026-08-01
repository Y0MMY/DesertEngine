#include "DrawList2D.hpp"

namespace Desert::Graphic::Render2D
{
    void DrawList2D::Reset()
    {
        m_Vertices.clear();
        m_Indices.clear();
        m_Commands.clear();
    }

    DrawCommand& DrawList2D::CurrentCommand( const void* texture, bool text )
    {
        if ( !m_Commands.empty() && m_Commands.back().Texture == texture && m_Commands.back().Text == text )
            return m_Commands.back();

        DrawCommand cmd;
        cmd.Texture     = texture;
        cmd.Text        = text;
        cmd.IndexOffset = static_cast<uint32_t>( m_Indices.size() );
        cmd.IndexCount  = 0;
        m_Commands.push_back( cmd );
        return m_Commands.back();
    }

    void DrawList2D::AddQuad( const void* texture, const glm::vec2& min, const glm::vec2& max,
                              const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec4& color, bool text )
    {
        // Open/extend the batch BEFORE appending indices so a freshly opened command anchors its
        // IndexOffset at this quad's first index (not past it).
        DrawCommand&   cmd  = CurrentCommand( texture, text );
        const uint32_t base = static_cast<uint32_t>( m_Vertices.size() );

        // Corners: top-left, top-right, bottom-right, bottom-left (CW in a top-left-origin, y-down space).
        m_Vertices.push_back( { { min.x, min.y }, { uv0.x, uv0.y }, color } );
        m_Vertices.push_back( { { max.x, min.y }, { uv1.x, uv0.y }, color } );
        m_Vertices.push_back( { { max.x, max.y }, { uv1.x, uv1.y }, color } );
        m_Vertices.push_back( { { min.x, max.y }, { uv0.x, uv1.y }, color } );

        const uint32_t quad[6] = { base + 0, base + 1, base + 2, base + 2, base + 3, base + 0 };
        m_Indices.insert( m_Indices.end(), quad, quad + 6 );

        cmd.IndexCount += 6;
    }

    void DrawList2D::AddRectFilled( const glm::vec2& min, const glm::vec2& max, const glm::vec4& color )
    {
        // Solid fill: null texture -> the backend binds its 1x1 white texel, so UVs are irrelevant (0..1).
        AddQuad( nullptr, min, max, { 0.0f, 0.0f }, { 1.0f, 1.0f }, color, false );
    }

    void DrawList2D::AddImage( const void* texture, const glm::vec2& min, const glm::vec2& max,
                               const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec4& tint )
    {
        AddQuad( texture, min, max, uv0, uv1, tint, false );
    }

    void DrawList2D::AddText( const void* atlas, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv0,
                              const glm::vec2& uv1, const glm::vec4& color )
    {
        AddQuad( atlas, min, max, uv0, uv1, color, true );
    }
} // namespace Desert::Graphic::Render2D
