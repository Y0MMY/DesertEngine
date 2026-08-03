#include "DrawList2D.hpp"

#include <algorithm>
#include <cmath>

namespace Desert::Graphic::Render2D
{
    void DrawList2D::Reset()
    {
        m_Vertices.clear();
        m_Indices.clear();
        m_Commands.clear();
        m_ClipStack.clear();
        m_CurrentClip = { 0.0f, 0.0f, 0.0f, 0.0f };
    }

    void DrawList2D::PushClipRect( const glm::vec2& min, const glm::vec2& max )
    {
        glm::vec4 r( min.x, min.y, max.x - min.x, max.y - min.y );
        if ( m_CurrentClip.z > 0.0f ) // intersect with the active clip so nested masks compose
        {
            const float x0 = std::max( m_CurrentClip.x, r.x );
            const float y0 = std::max( m_CurrentClip.y, r.y );
            const float x1 = std::min( m_CurrentClip.x + m_CurrentClip.z, r.x + r.z );
            const float y1 = std::min( m_CurrentClip.y + m_CurrentClip.w, r.y + r.w );
            r              = glm::vec4( x0, y0, std::max( 0.0f, x1 - x0 ), std::max( 0.0f, y1 - y0 ) );
        }
        m_ClipStack.push_back( m_CurrentClip );
        m_CurrentClip = r;
    }

    void DrawList2D::PopClipRect()
    {
        if ( m_ClipStack.empty() )
        {
            m_CurrentClip = { 0.0f, 0.0f, 0.0f, 0.0f };
            return;
        }
        m_CurrentClip = m_ClipStack.back();
        m_ClipStack.pop_back();
    }

    DrawCommand& DrawList2D::CurrentCommand( const void* texture, bool text )
    {
        if ( !m_Commands.empty() && m_Commands.back().Texture == texture && m_Commands.back().Text == text &&
             m_Commands.back().ClipRect == m_CurrentClip )
            return m_Commands.back();

        DrawCommand cmd;
        cmd.Texture     = texture;
        cmd.Text        = text;
        cmd.ClipRect    = m_CurrentClip;
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

    void DrawList2D::AddRectFilled( const glm::vec2& min, const glm::vec2& max, const glm::vec4& color,
                                    float rounding )
    {
        const float w = max.x - min.x, h = max.y - min.y;
        const float r = std::min( rounding, std::min( w, h ) * 0.5f );
        if ( r <= 0.5f )
        {
            // Sharp: null texture -> the backend binds its 1x1 white texel, so UVs are irrelevant (0..1).
            AddQuad( nullptr, min, max, { 0.0f, 0.0f }, { 1.0f, 1.0f }, color, false );
            return;
        }

        // Rounded: a triangle fan from the centre around a perimeter of four quarter-circle corner arcs (the
        // straight edges fall out between consecutive corner endpoints). y-down: angle 0=+x, PI/2=+y(down).
        constexpr float PI     = 3.14159265358979323846f;
        constexpr int   kSeg   = 6; // segments per corner
        DrawCommand&    cmd    = CurrentCommand( nullptr, false );
        const uint32_t  base   = static_cast<uint32_t>( m_Vertices.size() );
        const glm::vec2 centre = ( min + max ) * 0.5f;
        m_Vertices.push_back( { centre, { 0.5f, 0.5f }, color } );

        const glm::vec2 cc[4] = { { min.x + r, min.y + r },
                                  { max.x - r, min.y + r },
                                  { max.x - r, max.y - r },
                                  { min.x + r, max.y - r } };
        const float     a0[4] = { PI, PI * 1.5f, 0.0f, PI * 0.5f }; // each arc sweeps +PI/2, clockwise (y-down)

        uint32_t perim = 0;
        for ( int c = 0; c < 4; ++c )
            for ( int s = 0; s <= kSeg; ++s )
            {
                const float     a = a0[c] + ( PI * 0.5f ) * ( static_cast<float>( s ) / kSeg );
                const glm::vec2 p = cc[c] + glm::vec2( std::cos( a ), std::sin( a ) ) * r;
                m_Vertices.push_back( { p, { 0.5f, 0.5f }, color } );
                ++perim;
            }

        for ( uint32_t i = 0; i < perim; ++i ) // fan, closing the loop back to the first perimeter vertex
        {
            m_Indices.push_back( base );
            m_Indices.push_back( base + 1 + i );
            m_Indices.push_back( base + 1 + ( ( i + 1 ) % perim ) );
        }
        cmd.IndexCount += perim * 3;
    }

    void DrawList2D::AddImage( const void* texture, const glm::vec2& min, const glm::vec2& max,
                               const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec4& tint )
    {
        AddQuad( texture, min, max, uv0, uv1, tint, false );
    }

    void DrawList2D::AddRectFilledMultiColor( const glm::vec2& min, const glm::vec2& max,
                                              const glm::vec4& topColor, const glm::vec4& bottomColor )
    {
        DrawCommand&   cmd  = CurrentCommand( nullptr, false );
        const uint32_t base = static_cast<uint32_t>( m_Vertices.size() );

        // TL / TR carry the top colour, BR / BL the bottom colour -> a vertical gradient.
        m_Vertices.push_back( { { min.x, min.y }, { 0.0f, 0.0f }, topColor } );
        m_Vertices.push_back( { { max.x, min.y }, { 1.0f, 0.0f }, topColor } );
        m_Vertices.push_back( { { max.x, max.y }, { 1.0f, 1.0f }, bottomColor } );
        m_Vertices.push_back( { { min.x, max.y }, { 0.0f, 1.0f }, bottomColor } );

        const uint32_t quad[6] = { base + 0, base + 1, base + 2, base + 2, base + 3, base + 0 };
        m_Indices.insert( m_Indices.end(), quad, quad + 6 );
        cmd.IndexCount += 6;
    }

    void DrawList2D::AddRect( const glm::vec2& min, const glm::vec2& max, const glm::vec4& color, float thickness )
    {
        const float t = thickness;
        AddRectFilled( { min.x, min.y }, { max.x, min.y + t }, color );         // top
        AddRectFilled( { min.x, max.y - t }, { max.x, max.y }, color );         // bottom
        AddRectFilled( { min.x, min.y + t }, { min.x + t, max.y - t }, color ); // left
        AddRectFilled( { max.x - t, min.y + t }, { max.x, max.y - t }, color ); // right
    }

    void DrawList2D::AddText( const void* atlas, const glm::vec2& min, const glm::vec2& max, const glm::vec2& uv0,
                              const glm::vec2& uv1, const glm::vec4& color )
    {
        AddQuad( atlas, min, max, uv0, uv1, color, true );
    }

    void DrawList2D::AddTriangleFilled( const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2,
                                        const glm::vec4& color )
    {
        DrawCommand&   cmd  = CurrentCommand( nullptr, false );
        const uint32_t base = static_cast<uint32_t>( m_Vertices.size() );
        m_Vertices.push_back( { p0, { 0.5f, 0.5f }, color } );
        m_Vertices.push_back( { p1, { 0.5f, 0.5f }, color } );
        m_Vertices.push_back( { p2, { 0.5f, 0.5f }, color } );
        m_Indices.push_back( base + 0 );
        m_Indices.push_back( base + 1 );
        m_Indices.push_back( base + 2 );
        cmd.IndexCount += 3;
    }

    void DrawList2D::AddRing( const glm::vec2& center, float outerRadius, float innerRadius,
                              const glm::vec4& colorA, const glm::vec4& colorB, int segments )
    {
        if ( outerRadius <= 0.0f || segments < 3 )
            return;
        innerRadius = std::clamp( innerRadius, 0.0f, outerRadius );

        constexpr float TWO_PI = 6.28318530717958648f;
        DrawCommand&    cmd    = CurrentCommand( nullptr, false );
        const uint32_t  base   = static_cast<uint32_t>( m_Vertices.size() );

        // Two rims (outer, inner) per angular step; colour lerps A->B->A so the seam at 0/2PI is invisible.
        for ( int i = 0; i <= segments; ++i )
        {
            const float     f   = static_cast<float>( i ) / static_cast<float>( segments );
            const float     a   = TWO_PI * f;
            const float     t   = f < 0.5f ? f * 2.0f : ( 1.0f - f ) * 2.0f;
            const glm::vec4 col = colorA * ( 1.0f - t ) + colorB * t;
            const glm::vec2 dir( std::cos( a ), std::sin( a ) );
            m_Vertices.push_back( { center + dir * outerRadius, { 0.5f, 0.5f }, col } );
            m_Vertices.push_back( { center + dir * innerRadius, { 0.5f, 0.5f }, col } );
        }

        for ( int i = 0; i < segments; ++i ) // two triangles bridge rim pair i -> i+1
        {
            const uint32_t o0 = base + i * 2, in0 = base + i * 2 + 1;
            const uint32_t o1 = base + ( i + 1 ) * 2, in1 = base + ( i + 1 ) * 2 + 1;
            m_Indices.push_back( o0 );
            m_Indices.push_back( o1 );
            m_Indices.push_back( in1 );
            m_Indices.push_back( o0 );
            m_Indices.push_back( in1 );
            m_Indices.push_back( in0 );
            cmd.IndexCount += 6;
        }
    }
} // namespace Desert::Graphic::Render2D
