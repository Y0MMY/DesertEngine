#include "DrawList2D.hpp"

namespace Desert::Graphic::Render2D
{
    void DrawList2D::Reset()
    {
        m_Vertices.clear();
        m_Indices.clear();
        m_Commands.clear();
    }

    DrawCommand& DrawList2D::CurrentCommand( const void* texture )
    {
        if ( !m_Commands.empty() && m_Commands.back().Texture == texture )
            return m_Commands.back();

        DrawCommand cmd;
        cmd.Texture     = texture;
        cmd.IndexOffset = static_cast<uint32_t>( m_Indices.size() );
        cmd.IndexCount  = 0;
        m_Commands.push_back( cmd );
        return m_Commands.back();
    }

    void DrawList2D::AddRectFilled( const glm::vec2& min, const glm::vec2& max, const glm::vec4& color )
    {
        // Open/extend the batch BEFORE appending indices so a freshly opened command anchors its
        // IndexOffset at this quad's first index (not past it).
        DrawCommand&   cmd  = CurrentCommand( nullptr );
        const uint32_t base = static_cast<uint32_t>( m_Vertices.size() );

        // Corners: top-left, top-right, bottom-right, bottom-left (CW in a top-left-origin, y-down space).
        m_Vertices.push_back( { { min.x, min.y }, { 0.0f, 0.0f }, color } );
        m_Vertices.push_back( { { max.x, min.y }, { 1.0f, 0.0f }, color } );
        m_Vertices.push_back( { { max.x, max.y }, { 1.0f, 1.0f }, color } );
        m_Vertices.push_back( { { min.x, max.y }, { 0.0f, 1.0f }, color } );

        const uint32_t quad[6] = { base + 0, base + 1, base + 2, base + 2, base + 3, base + 0 };
        m_Indices.insert( m_Indices.end(), quad, quad + 6 );

        cmd.IndexCount += 6;
    }
} // namespace Desert::Graphic::Render2D
