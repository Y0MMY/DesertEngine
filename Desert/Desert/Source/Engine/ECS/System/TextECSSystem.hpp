#pragma once

#include "System.hpp"

#include <Common/Core/Logger.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Font/FontService.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Graphic/Render/Commands/DrawGenericMeshCommand.hpp>

#include <limits>

namespace Desert::ECS
{
    // Lays each TextComponent's string into a quad mesh (one quad per glyph, in the entity's local
    // space) and draws it through the generic path with the TextSDF shader + the font's SDF atlas.
    // Render-data collector only (no structural changes, no shared writes) -> parallel-safe.
    class TextECSSystem : public System
    {
    public:
        bool CanRunParallel() const override { return true; }

        void Update( entt::registry& registry, Graphic::Render::RenderCommandBuffer& renderCommandBuffer,
                     const Common::Timestep& ts ) override
        {
            auto view = registry.view<TextComponent, TransformComponent>();
            view.each(
                 [&]( entt::entity entity, TextComponent& text, const TransformComponent& transform )
                 {
                     if ( registry.has<VisibilityComponent>( entity ) &&
                          !registry.get<VisibilityComponent>( entity ).Visible )
                         return;
                     if ( text.Text.empty() )
                         return;

                     auto* font = Runtime::ResourceRegistry::GetFontService()->Get( text.FontPath );
                     if ( !font )
                         return;

                     // Rebuild the glyph mesh only when the laid-out result would differ.
                     if ( !text.RuntimeMesh || text.BuiltText != text.Text ||
                          text.BuiltFont != text.FontPath || text.BuiltSize != text.Size )
                     {
                         text.RuntimeMesh = BuildTextMesh( text, font->Baked );
                         text.BuiltText   = text.Text;
                         text.BuiltFont   = text.FontPath;
                         text.BuiltSize   = text.Size;
                     }
                     if ( !text.RuntimeMesh )
                         return;

                     // WORLD transform (walk the parent chain), mirroring the mesh systems.
                     glm::mat4    worldTransform = transform.GetTransform();
                     entt::entity current        = entity;
                     while ( registry.has<RelationshipComponent>( current ) )
                     {
                         const auto& rel = registry.get<RelationshipComponent>( current );
                         if ( rel.Parent == entt::null )
                             break;
                         current = rel.Parent;
                         if ( registry.has<TransformComponent>( current ) )
                             worldTransform =
                                  registry.get<TransformComponent>( current ).GetTransform() * worldTransform;
                     }

                     Graphic::MaterialOverrides overrides;
                     overrides.Params.emplace_back( "TextColor", text.Color );
                     overrides.Params.emplace_back( "EmissiveIntensity",
                                                    glm::vec4( text.EmissiveIntensity, 0, 0, 0 ) );

                     renderCommandBuffer.Emplace<Graphic::Render::DrawGenericMeshCommand>(
                          text.RuntimeMesh.get(), worldTransform, std::string( "TextSDF" ),
                          std::move( overrides ), /*outlined*/ false, font->Atlas.get(),
                          std::string( "u_SDFAtlas" ) );
                 } );
        }

    private:
        static std::shared_ptr<DynamicMesh> BuildTextMesh( const TextComponent& text,
                                                           const Text::BakedFont& font )
        {
            const float worldPerPixel = ( font.PixelHeight > 0.0f ) ? text.Size / font.PixelHeight : 0.0f;
            if ( worldPerPixel <= 0.0f )
                return nullptr;

            std::vector<Vertex> verts;
            std::vector<Index>  inds;
            verts.reserve( text.Text.size() * 4 );
            inds.reserve( text.Text.size() * 2 );

            glm::vec3 mn( std::numeric_limits<float>::max() );
            glm::vec3 mx( std::numeric_limits<float>::lowest() );

            float penX = 0.0f, penY = 0.0f; // pixels; baseline at penY, +X right, lines step -Y
            const float lineStep = font.LineHeight();

            for ( char ch : text.Text )
            {
                if ( ch == '\n' )
                {
                    penX = 0.0f;
                    penY -= lineStep;
                    continue;
                }
                const auto it = font.Glyphs.find( static_cast<uint32_t>( static_cast<unsigned char>( ch ) ) );
                if ( it == font.Glyphs.end() )
                    continue;
                const Text::Glyph& g = it->second;

                if ( g.Width > 0.0f && g.Height > 0.0f )
                {
                    // OffsetY is the top of the bitmap relative to the baseline (Y-down); flip to Y-up world.
                    const float x0   = ( penX + g.OffsetX ) * worldPerPixel;
                    const float x1   = x0 + g.Width * worldPerPixel;
                    const float yTop = ( penY - g.OffsetY ) * worldPerPixel;
                    const float yBot = yTop - g.Height * worldPerPixel;

                    const uint32_t base = static_cast<uint32_t>( verts.size() );
                    // The quad's local +Z faces the default camera (at +Z looking -Z); that view maps
                    // world +X to SCREEN-left, so a +X layout reads mirrored. Negate X to lay the text
                    // out so it reads left-to-right head-on (a flat label is mirrored from behind — as
                    // expected for any single plane of text).
                    auto push = [&]( float x, float y, float u, float v )
                    {
                        Vertex vtx{};
                        vtx.Position = { -x, y, 0.0f };
                        vtx.Normal   = { 0.0f, 0.0f, 1.0f };
                        vtx.TexCoord = { u, v };
                        verts.push_back( vtx );
                        mn = glm::min( mn, vtx.Position );
                        mx = glm::max( mx, vtx.Position );
                    };
                    push( x0, yTop, g.U0, g.V0 ); // TL
                    push( x1, yTop, g.U1, g.V0 ); // TR
                    push( x1, yBot, g.U1, g.V1 ); // BR
                    push( x0, yBot, g.U0, g.V1 ); // BL
                    inds.push_back( { base + 0, base + 3, base + 2 } );
                    inds.push_back( { base + 0, base + 2, base + 1 } );
                }
                penX += g.Advance;
            }

            if ( verts.empty() )
                return nullptr;

            Common::Math::AABB aabb;
            aabb.Min = mn;
            aabb.Max = mx;
            std::vector<Submesh> subs = { { "Text", 0, static_cast<uint32_t>( verts.size() ), 0,
                                           static_cast<uint32_t>( inds.size() ) * 3, glm::mat4( 1.0f ),
                                           aabb } };

            auto mesh = std::make_shared<DynamicMesh>( verts, inds, subs, /*generateLODs*/ false );
            mesh->Invalidate();
            return mesh;
        }
    };
} // namespace Desert::ECS
