#include "MeshDetailsWidget.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>
#include <ImGui/imgui.h>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/Geometry/LODSelection.hpp>
#include <Engine/Geometry/Mesh.hpp>

#include <algorithm>
#include <string>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        // What the GPU mesh actually holds. Everything here is read straight off the built Submeshes —
        // no asset-side guessing, so the numbers describe what is drawn.
        struct MeshStats
        {
            uint32_t  Elements  = 0;
            uint64_t  Vertices  = 0;
            uint64_t  Triangles = 0; // LOD 0
            uint32_t  LODLevels = 1;
            glm::vec3 Extent    = glm::vec3( 0.0f );
            bool      HasBounds = false;
        };

        MeshStats ComputeStats( const ::Desert::Mesh& mesh )
        {
            MeshStats stats;
            glm::vec3 mn( 1.0e30f );
            glm::vec3 mx( -1.0e30f );

            for ( const auto& sm : mesh.GetSubmeshes() )
            {
                ++stats.Elements;
                stats.Vertices += sm.VertexCount;
                // Triangles come from the INDEX count: a vertex is shared by several triangles, so
                // VertexCount/3 undercounts an indexed mesh badly.
                stats.Triangles += sm.IndexCount / 3;
                stats.LODLevels = std::max( stats.LODLevels, static_cast<uint32_t>( sm.LODs.size() ) );

                mn = glm::min( mn, sm.BoundingBox.Min );
                mx = glm::max( mx, sm.BoundingBox.Max );
            }

            if ( mn.x <= mx.x )
            {
                stats.Extent    = mx - mn;
                stats.HasBounds = true;
            }
            return stats;
        }

        // 1234567 -> "1 234 567". Big triangle counts are unreadable as a raw run of digits, and that
        // readout is the whole point of the section.
        std::string FormatCount( uint64_t value )
        {
            std::string digits = std::to_string( value );
            std::string out;
            out.reserve( digits.size() + digits.size() / 3 );
            const size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
            for ( size_t i = 0; i < digits.size(); ++i )
            {
                if ( i > 0 && ( i - lead ) % 3 == 0 )
                    out += ' ';
                out += digits[i];
            }
            return out;
        }

        void StatRow( const char* label, const std::string& value, const char* tooltip = nullptr )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( label );
            if ( tooltip )
                Utils::ImGuiUtilities::Tooltip( tooltip );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( value.c_str() );
            if ( tooltip )
                Utils::ImGuiUtilities::Tooltip( tooltip );
        }

        std::string FormatExtent( const glm::vec3& e )
        {
            char buf[96];
            std::snprintf( buf, sizeof( buf ), "%.1f x %.1f x %.1f cm", e.x, e.y, e.z );
            return buf;
        }

        // Per-axis scale of a world transform (basis vector lengths) — the mesh's local bounds times
        // this is the size the object actually occupies in the level.
        glm::vec3 TransformScale( const glm::mat4& m )
        {
            return { glm::length( glm::vec3( m[0] ) ), glm::length( glm::vec3( m[1] ) ),
                     glm::length( glm::vec3( m[2] ) ) };
        }

        std::string DescribeCollision( const ECS::Entity& entity )
        {
            if ( !entity.HasComponent<ECS::ColliderComponent>() )
                return "None";

            const auto& c = entity.GetComponent<ECS::ColliderComponent>().Data;
            char        buf[128];
            switch ( c.Shape )
            {
                case Physics::ShapeType::Sphere:
                    std::snprintf( buf, sizeof( buf ), "Sphere - radius %.1f cm", c.Radius );
                    break;
                case Physics::ShapeType::Capsule:
                    std::snprintf( buf, sizeof( buf ), "Capsule - radius %.1f cm, half height %.1f cm", c.Radius,
                                   c.HalfHeight );
                    break;
                case Physics::ShapeType::Box:
                default:
                    std::snprintf( buf, sizeof( buf ), "Box - %.1f x %.1f x %.1f cm", c.HalfExtents.x * 2.0f,
                                   c.HalfExtents.y * 2.0f, c.HalfExtents.z * 2.0f );
                    break;
            }
            return buf;
        }
    } // namespace

    void MeshDetailsWidget::Show( const Context& ctx )
    {
        if ( !ImGui::CollapsingHeader( ICON_MDI_SHAPE " Mesh", ImGuiTreeNodeFlags_DefaultOpen ) )
            return;

        ImGui::Indent();

        if ( ctx.Asset )
        {
            const std::string path = ctx.Asset->GetMetadata().Filepath.string();
            ImGui::TextDisabled( "%s", path.c_str() );
            Utils::ImGuiUtilities::Tooltip( path.c_str() );
        }

        // The RUNTIME (GPU) mesh builds lazily and can be null (not built yet, or a skinned mesh whose
        // build hasn't run). Say so rather than showing zeros that read like an empty mesh.
        if ( !ctx.RuntimeMesh )
        {
            ImGui::TextDisabled( "Mesh not built yet..." );
            ImGui::Unindent();
            return;
        }

        const MeshStats stats = ComputeStats( *ctx.RuntimeMesh );

        if ( ImGui::BeginTable( "##mesh_stats", 2,
                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings ) )
        {
            ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthStretch, 0.38f );
            ImGui::TableSetupColumn( "value", ImGuiTableColumnFlags_WidthStretch, 0.62f );

            StatRow( "Triangles", FormatCount( stats.Triangles ), "Triangle count of LOD 0 (all elements)" );
            StatRow( "Vertices", FormatCount( stats.Vertices ) );
            StatRow( "Elements", FormatCount( stats.Elements ),
                     "Submeshes. Each one takes its material from the slot of the same index." );

            // LOD: how many levels the chain has and which one is on screen RIGHT NOW — computed with
            // the renderer's own policy (Geometry::SelectLOD), so it can't drift from what is drawn.
            {
                std::string lod = FormatCount( stats.LODLevels ) + ( stats.LODLevels == 1 ? " level" : " levels" );
                // With a single level there is nothing to resolve — "drawing LOD 0" would be noise.
                if ( stats.LODLevels > 1 )
                {
                    if ( ctx.ForcedLOD >= 0 )
                    {
                        const uint32_t forced =
                             std::min( static_cast<uint32_t>( ctx.ForcedLOD ), stats.LODLevels - 1 );
                        lod += "  -  drawing LOD " + std::to_string( forced ) + " (forced)";
                    }
                    else if ( ctx.Scene && ctx.Entity )
                    {
                        if ( const auto& camera = ctx.Scene->GetActiveCamera() )
                        {
                            const uint32_t active =
                                 std::min( Geometry::SelectLOD( ctx.Entity->GetWorldTransform(),
                                                                ctx.RuntimeMesh->GetSubmeshes(),
                                                                camera->GetPosition(), -1, ctx.LODBias ),
                                           stats.LODLevels - 1 );
                            lod += "  -  drawing LOD " + std::to_string( active ) + " (auto)";
                        }
                    }
                }
                StatRow( "LOD", lod, "Levels in the chain, and the one the current view resolves to" );
            }

            if ( stats.HasBounds )
            {
                StatRow( "Bounds", FormatExtent( stats.Extent ), "Local-space extent of the mesh" );

                if ( ctx.Entity )
                {
                    const glm::vec3 scale = TransformScale( ctx.Entity->GetWorldTransform() );
                    if ( glm::any(
                              glm::greaterThan( glm::abs( scale - glm::vec3( 1.0f ) ), glm::vec3( 0.001f ) ) ) )
                        StatRow( "World size", FormatExtent( stats.Extent * scale ),
                                 "Bounds after the entity's world scale" );
                }
            }

            StatRow( "UV channels", "1",
                     "The engine vertex format carries a single UV set (Vertex::TexCoord), so a mesh "
                     "exposes one channel however many the source file had." );

            if ( ctx.Entity )
                StatRow( "Collision", DescribeCollision( *ctx.Entity ),
                         "Collider component on this entity (physics uses this shape, not the mesh)" );

            ImGui::EndTable();
        }

        // Per-element detail, folded: which element is which, and how heavy each one is.
        if ( stats.Elements > 0 && ImGui::TreeNodeEx( "Elements", ImGuiTreeNodeFlags_Framed ) )
        {
            if ( ImGui::BeginTable( "##mesh_elements", 4,
                                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_NoSavedSettings ) )
            {
                ImGui::TableSetupColumn( "Element", ImGuiTableColumnFlags_WidthStretch, 0.40f );
                ImGui::TableSetupColumn( "Tris", ImGuiTableColumnFlags_WidthStretch, 0.22f );
                ImGui::TableSetupColumn( "Verts", ImGuiTableColumnFlags_WidthStretch, 0.22f );
                ImGui::TableSetupColumn( "LODs", ImGuiTableColumnFlags_WidthStretch, 0.16f );
                ImGui::TableHeadersRow();

                uint32_t index = 0;
                for ( const auto& sm : ctx.RuntimeMesh->GetSubmeshes() )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const std::string name = sm.Name.empty() ? ( "Element " + std::to_string( index ) )
                                                             : ( std::to_string( index ) + "  " + sm.Name );
                    ImGui::TextUnformatted( name.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( FormatCount( sm.IndexCount / 3 ).c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( FormatCount( sm.VertexCount ).c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( FormatCount( std::max<size_t>( sm.LODs.size(), 1 ) ).c_str() );
                    ++index;
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }

        ImGui::Unindent();
    }

} // namespace Desert::Editor
