#include "UICanvasLayout.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Render2D/Transform2D.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Desert::UI
{
    entt::entity CanvasOf( entt::registry& reg, entt::entity e )
    {
        // Bounded by the entity count rather than trusting the tree to be acyclic: a Parent cycle is
        // authorable (the hierarchy panel can reparent), and an unbounded walk here would hang the editor
        // instead of returning "not under a canvas".
        const std::size_t limit = reg.size() + 1;
        std::size_t       steps = 0;
        for ( entt::entity cur = e; cur != entt::null && reg.valid( cur ) && steps < limit; ++steps )
        {
            if ( reg.has<ECS::UICanvasComponent>( cur ) )
                return cur;
            cur = reg.has<ECS::RelationshipComponent>( cur ) ? reg.get<ECS::RelationshipComponent>( cur ).Parent
                                                             : entt::null;
        }
        return entt::null;
    }

    std::size_t CanvasCount( entt::registry& reg )
    {
        std::size_t n = 0;
        for ( [[maybe_unused]] const auto e : reg.view<ECS::UICanvasComponent>() )
            ++n;
        return n;
    }

    Common::ResultStr<entt::entity> SoleCanvas( entt::registry& reg )
    {
        entt::entity first = entt::null;
        std::size_t  n     = 0;
        for ( const auto e : reg.view<ECS::UICanvasComponent>() )
        {
            if ( n == 0 )
                first = e;
            ++n;
        }
        if ( n == 1 )
            return Common::MakeSuccess( first );
        if ( n == 0 )
            return Common::MakeFormattedError<entt::entity>(
                 "[UI] the scene has no UI canvas, so no host can be given one to draw or measure" );
        return Common::MakeFormattedError<entt::entity>(
             "[UI] the scene has {} UI canvases and this host did not name one; it is NOT the first one's "
             "job to win by iteration order — name the canvas (UI::CanvasOf on an element of it, or the "
             "host's own document/subject)",
             n );
    }

    bool TakesLayoutSpace( entt::registry& reg, entt::entity e )
    {
        if ( !reg.valid( e ) || !reg.has<ECS::UILayoutComponent>( e ) )
            return true;
        return reg.get<ECS::UILayoutComponent>( e ).Data.Visibility != ECS::UIVisibility::Collapsed;
    }

    bool IsElementVisible( entt::registry& reg, entt::entity e )
    {
        if ( !reg.valid( e ) || !reg.has<ECS::UILayoutComponent>( e ) )
            return true;
        return reg.get<ECS::UILayoutComponent>( e ).Data.Visibility == ECS::UIVisibility::Visible;
    }

    namespace
    {
        // Maps the canvas to the viewport per its scale mode (see UICanvasScaleMode). Returns the canvas root
        // rect (screen px) + a uniform scale applied to every element's offsets/min-size/font — so Stretch is
        // 1:1 (anchors drive layout, no resize zoom), ScaleWithScreen scales the whole design from the
        // reference resolution, and Letterbox fits + centres it. Mirrors RenderCanvas2D's own ResolveCanvas.
        struct CanvasFit
        {
            Rect  Root;
            float Scale;
        };

        CanvasFit ResolveCanvas( const ECS::UICanvasData& d, const Rect& viewportPx )
        {
            switch ( d.ScaleMode )
            {
                case ECS::UICanvasScaleMode::ScaleWithScreen:
                {
                    const float sx = d.ReferenceWidth > 0.0f ? viewportPx.W / d.ReferenceWidth : 1.0f;
                    const float sy = d.ReferenceHeight > 0.0f ? viewportPx.H / d.ReferenceHeight : 1.0f;
                    const float m  = std::clamp( d.MatchWidthHeight, 0.0f, 1.0f );
                    return { viewportPx, sx * ( 1.0f - m ) + sy * m };
                }
                case ECS::UICanvasScaleMode::Letterbox:
                {
                    const Rect fit = CanvasRect( d.ReferenceWidth, d.ReferenceHeight, viewportPx.W, viewportPx.H );
                    const float scale = d.ReferenceWidth > 0.0f ? fit.W / d.ReferenceWidth : 1.0f;
                    return { Rect{ viewportPx.X + fit.X, viewportPx.Y + fit.Y, fit.W, fit.H }, scale };
                }
                case ECS::UICanvasScaleMode::Stretch:
                default:
                    return { viewportPx, 1.0f }; // canvas == viewport, 1:1 px
            }
        }

        // Resolves the root rect of the canvas the caller NAMED, exactly like the renderer. Shared by
        // PickElement / GetElementRect / CanvasScale so hit-testing matches drawing.
        //
        // The three refusals are distinct on purpose. "You named something that is not a canvas" is a caller
        // bug and reads nothing like "this canvas is switched off", and both used to arrive as the same
        // `false` — on top of a canvas nobody had named in the first place.
        Common::ResultStr<CanvasFit> ResolveNamedCanvas( entt::registry& reg, entt::entity canvas,
                                                         const Rect& viewportPx )
        {
            if ( canvas == entt::null || !reg.valid( canvas ) )
                return Common::MakeFormattedError<CanvasFit>(
                     "[UI] no canvas was named for this layout query (entity {})",
                     static_cast<std::uint32_t>( canvas ) );
            if ( !reg.has<ECS::UICanvasComponent>( canvas ) )
                return Common::MakeFormattedError<CanvasFit>(
                     "[UI] entity {} was named as a canvas but carries no UICanvasComponent",
                     static_cast<std::uint32_t>( canvas ) );

            const auto& canvasData = reg.get<ECS::UICanvasComponent>( canvas ).Data;
            if ( !canvasData.Visible )
                return Common::MakeFormattedError<CanvasFit>(
                     "[UI] canvas {} is not Visible, so it has no on-screen layout to report",
                     static_cast<std::uint32_t>( canvas ) );

            return Common::MakeSuccess( ResolveCanvas( canvasData, viewportPx ) );
        }

        // Content size (px) a layout-group container needs to hug its children — mirrors the renderer's
        // GroupContentPx so the Content Size Fitter picks with the same rect it draws.
        glm::vec2 GroupContentPx( entt::registry& reg, entt::entity e, float scale )
        {
            if ( !reg.has<ECS::UILayoutGroupComponent>( e ) || !reg.has<ECS::RelationshipComponent>( e ) )
                return { 0.0f, 0.0f };
            const auto&            g = reg.get<ECS::UILayoutGroupComponent>( e ).Data;
            std::vector<glm::vec2> sizes;
            for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
            {
                if ( !reg.valid( c ) || !TakesLayoutSpace( reg, c ) )
                    continue; // a Collapsed child is not in the group, so it is not in its content size
                glm::vec2 pref( 0.0f );
                if ( reg.has<ECS::UILayoutComponent>( c ) )
                {
                    const auto& L = reg.get<ECS::UILayoutComponent>( c ).Data;
                    pref          = glm::max( L.CustomMinimumSize, L.OffsetMax - L.OffsetMin );
                }
                sizes.push_back( pref * scale );
            }
            LayoutGroupParams params;
            params.Type     = g.Type == ECS::UILayoutType::Horizontal ? LayoutGroupType::Horizontal
                              : g.Type == ECS::UILayoutType::Grid     ? LayoutGroupType::Grid
                                                                      : LayoutGroupType::Vertical;
            params.PaddingL = g.Padding.x * scale;
            params.PaddingT = g.Padding.y * scale;
            params.PaddingR = g.Padding.z * scale;
            params.PaddingB = g.Padding.w * scale;
            params.Spacing  = g.Spacing * scale;
            params.CellSize = g.CellSize * scale;
            params.Columns  = g.Columns;
            return MeasureLayoutGroup( params, sizes );
        }

        // If `e` is an auto-layout container, solve its children's rects exactly like the renderer's
        // DrawElement does — so hit-testing / handles match the drawn positions (children of a VBox/HBox/Grid
        // are placed by the group, NOT their own anchors). Fills kids + one rect each; empty when not a group.
        void SolveGroupChildren( entt::registry& reg, entt::entity e, const Rect& container, float scale,
                                 std::vector<entt::entity>& kids, std::vector<Rect>& rects )
        {
            if ( !reg.has<ECS::UILayoutGroupComponent>( e ) || !reg.has<ECS::RelationshipComponent>( e ) )
                return;
            const auto&            g = reg.get<ECS::UILayoutGroupComponent>( e ).Data;
            std::vector<glm::vec2> sizes;
            std::vector<float>     flex;
            for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
            {
                if ( !reg.valid( c ) || !TakesLayoutSpace( reg, c ) )
                    continue; // Collapsed: no slot here, exactly as in the renderer's own group solve
                glm::vec2 pref( 0.0f );
                float     fg = 0.0f;
                if ( reg.has<ECS::UILayoutComponent>( c ) )
                {
                    const auto& L = reg.get<ECS::UILayoutComponent>( c ).Data;
                    pref          = glm::max( L.CustomMinimumSize, L.OffsetMax - L.OffsetMin );
                    fg            = L.FlexGrow;
                }
                kids.push_back( c );
                sizes.push_back( pref * scale );
                flex.push_back( fg );
            }
            LayoutGroupParams params;
            params.Type         = g.Type == ECS::UILayoutType::Horizontal ? LayoutGroupType::Horizontal
                                  : g.Type == ECS::UILayoutType::Grid     ? LayoutGroupType::Grid
                                                                          : LayoutGroupType::Vertical;
            params.PaddingL     = g.Padding.x * scale;
            params.PaddingT     = g.Padding.y * scale;
            params.PaddingR     = g.Padding.z * scale;
            params.PaddingB     = g.Padding.w * scale;
            params.Spacing      = g.Spacing * scale;
            params.StretchCross = g.StretchCross;
            params.CellSize     = g.CellSize * scale;
            params.Columns      = g.Columns;
            rects               = SolveLayoutGroup( container, params, sizes, flex );
        }

        // The element's accumulated transform: its parent's, with its own composed inside it. THE SAME
        // COMPOSITION ORDER THE RENDERER USES (DrawList2D::PushTransform), because a pick that composed
        // the other way round would be right for one level and wrong for two.
        glm::mat3 AccumulateTransform( entt::registry& reg, entt::entity e, const Rect& rect,
                                       const glm::mat3& parentXform )
        {
            if ( !reg.has<ECS::UILayoutComponent>( e ) )
                return parentXform;
            const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
            if ( L.Rotation == 0.0f && L.Scale == glm::vec2( 1.0f, 1.0f ) )
                return parentXform;
            const glm::vec2 pivotPx( rect.X + L.Pivot.x * rect.W, rect.Y + L.Pivot.y * rect.H );
            return parentXform * Graphic::Render2D::MakeTransform2D( pivotPx, L.Rotation, L.Scale );
        }

        // The pointer brought into @p xform's space — the inverse of what the geometry went through, so
        // "is the cursor inside this element" is asked about the rect the element actually has.
        glm::vec2 UndoTransform( const glm::mat3& xform, const glm::vec2& p )
        {
            return Graphic::Render2D::IsIdentity2D( xform )
                        ? p
                        : Graphic::Render2D::TransformPoint2D( Graphic::Render2D::InverseTransform2D( xform ), p );
        }

        void PickRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale, const glm::vec2& p,
                          entt::entity& hit, const glm::mat3& parentXform, const Rect* forcedRect = nullptr )
        {
            // An element that is not drawn cannot be clicked in the viewport either — the same rule the
            // renderer applies to the pointer, applied to the editor's WYSIWYG pick, because a marquee
            // appearing around something invisible is a selection the author cannot explain. A hidden
            // element is still selectable from the Scene Hierarchy, which is where it is visible.
            if ( !IsElementVisible( reg, e ) )
                return;

            Rect       rect      = parent;
            const bool hasLayout = reg.has<ECS::UILayoutComponent>( e );
            if ( forcedRect )
                rect = *forcedRect; // positioned by a parent auto-layout group
            else if ( hasLayout )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );
            }
            if ( hasLayout ) // match the renderer's fitters so hit-testing lines up
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ApplyAspectFit( rect, L.AspectRatio, static_cast<int>( L.AspectMode ) );
                if ( ( L.FitWidth || L.FitHeight ) && reg.has<ECS::UILayoutGroupComponent>( e ) )
                {
                    const glm::vec2 content = GroupContentPx( reg, e, scale );
                    if ( L.FitWidth )
                        rect.W = content.x;
                    if ( L.FitHeight )
                        rect.H = content.y;
                }
            }
            // The element's render transform, composed onto its ancestors' — so a child of a rotated
            // panel is picked where the panel carried it, not where its own anchors put it.
            const glm::mat3 xform = AccumulateTransform( reg, e, rect, parentXform );
            const glm::vec2 local = UndoTransform( xform, p );

            // Any element with a rect is selectable; later/deeper hits overwrite (matches draw order), so a
            // small button on top of a full-screen panel wins the pick instead of the panel behind it.
            if ( ( forcedRect || hasLayout ) && local.x >= rect.X && local.x <= rect.X + rect.W &&
                 local.y >= rect.Y && local.y <= rect.Y + rect.H )
                hit = e;

            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                std::vector<entt::entity> kids;
                std::vector<Rect>         rects;
                SolveGroupChildren( reg, e, rect, scale, kids, rects );
                if ( !kids.empty() )
                    for ( std::size_t i = 0; i < kids.size(); ++i )
                        PickRecurse( reg, kids[i], rect, scale, p, hit, xform, &rects[i] );
                else
                    for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                        if ( reg.valid( c ) )
                            PickRecurse( reg, c, rect, scale, p, hit, xform );
            }
        }

        void RectRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale,
                          entt::entity target, Rect& out, bool& found, const glm::mat3& parentXform,
                          glm::mat3* outXform, const Rect* forcedRect = nullptr )
        {
            Rect rect = parent;
            if ( forcedRect )
                rect = *forcedRect;
            else if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin * scale, L.OffsetMax * scale,
                                             L.CustomMinimumSize * scale, parent );
            }
            if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect          = ApplyAspectFit( rect, L.AspectRatio, static_cast<int>( L.AspectMode ) );
                if ( ( L.FitWidth || L.FitHeight ) && reg.has<ECS::UILayoutGroupComponent>( e ) )
                {
                    const glm::vec2 content = GroupContentPx( reg, e, scale );
                    if ( L.FitWidth )
                        rect.W = content.x;
                    if ( L.FitHeight )
                        rect.H = content.y;
                }
            }
            const glm::mat3 xform = AccumulateTransform( reg, e, rect, parentXform );
            if ( e == target )
            {
                out   = rect;
                found = true;
                if ( outXform )
                    *outXform = xform;
                return;
            }
            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                std::vector<entt::entity> kids;
                std::vector<Rect>         rects;
                SolveGroupChildren( reg, e, rect, scale, kids, rects );
                if ( !kids.empty() )
                    for ( std::size_t i = 0; i < kids.size() && !found; ++i )
                        RectRecurse( reg, kids[i], rect, scale, target, out, found, xform, outXform, &rects[i] );
                else
                    for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                        if ( reg.valid( c ) && !found )
                            RectRecurse( reg, c, rect, scale, target, out, found, xform, outXform );
            }
        }
    } // namespace

    entt::entity PickElement( entt::registry& reg, entt::entity canvas, const glm::vec2& pointPx,
                              const Rect& viewportPx )
    {
        const auto fit = ResolveNamedCanvas( reg, canvas, viewportPx );
        if ( !fit )
            return entt::null; // nothing on screen to hit; the refusal's text belongs to the caller's own log

        const float scale     = fit.GetValue().Scale;
        const auto& cd        = reg.get<ECS::UICanvasComponent>( canvas ).Data;
        const Rect  childRoot = InsetRect( fit.GetValue().Root, cd.SafeArea.x * scale, cd.SafeArea.y * scale,
                                           cd.SafeArea.z * scale, cd.SafeArea.w * scale );

        entt::entity hit = entt::null;
        if ( reg.has<ECS::RelationshipComponent>( canvas ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvas ).Children )
                if ( reg.valid( c ) )
                    PickRecurse( reg, c, childRoot, scale, pointPx, hit, glm::mat3( 1.0f ) );
        return hit;
    }

    bool GetElementRect( entt::registry& reg, entt::entity canvas, entt::entity target, const Rect& viewportPx,
                         Rect& out, glm::mat3* outXform )
    {
        // Cleared up front, so a caller that reads it after a `false` gets the identity rather than
        // whatever it happened to hold — and so `target == canvas` below reports one too.
        if ( outXform )
            *outXform = glm::mat3( 1.0f );

        const auto fit = ResolveNamedCanvas( reg, canvas, viewportPx );
        if ( !fit )
            return false;

        if ( target == canvas )
        {
            out = fit.GetValue().Root;
            return true;
        }
        const float scale     = fit.GetValue().Scale;
        const auto& cd        = reg.get<ECS::UICanvasComponent>( canvas ).Data;
        const Rect  childRoot = InsetRect( fit.GetValue().Root, cd.SafeArea.x * scale, cd.SafeArea.y * scale,
                                           cd.SafeArea.z * scale, cd.SafeArea.w * scale );
        bool        found     = false;
        if ( reg.has<ECS::RelationshipComponent>( canvas ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvas ).Children )
                if ( reg.valid( c ) && !found )
                    RectRecurse( reg, c, childRoot, scale, target, out, found, glm::mat3( 1.0f ), outXform );
        return found;
    }

    Common::ResultStr<float> CanvasScale( entt::registry& reg, entt::entity canvas, const Rect& viewportPx )
    {
        const auto fit = ResolveNamedCanvas( reg, canvas, viewportPx );
        if ( !fit )
            return Common::MakeError<float>( fit.GetError() );
        return Common::MakeSuccess( fit.GetValue().Scale );
    }
} // namespace Desert::UI
