#include "UICanvasLayout.hpp"

#include <Engine/ECS/Components.hpp>

#include <algorithm>
#include <vector>

namespace Desert::UI
{
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

        // Resolves the canvas root rect exactly like the renderer. false if the scene has no visible canvas.
        // Shared by PickElement / GetElementRect so hit-testing matches drawing.
        bool CanvasRootRect( entt::registry& reg, const Rect& viewportPx, entt::entity& canvasOut, Rect& rectOut,
                             float& scaleOut )
        {
            auto canvasView = reg.view<ECS::UICanvasComponent>();
            if ( canvasView.begin() == canvasView.end() )
                return false;

            const entt::entity canvasEntity = *canvasView.begin();
            const auto&        canvasData   = reg.get<ECS::UICanvasComponent>( canvasEntity ).Data;
            if ( !canvasData.Visible )
                return false;

            const CanvasFit fit = ResolveCanvas( canvasData, viewportPx );
            rectOut             = fit.Root;
            scaleOut            = fit.Scale;
            canvasOut           = canvasEntity;
            return true;
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
                if ( !reg.valid( c ) )
                    continue;
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
                if ( !reg.valid( c ) )
                    continue;
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

        void PickRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale, const glm::vec2& p,
                          entt::entity& hit, const Rect* forcedRect = nullptr )
        {
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
            // Any element with a rect is selectable; later/deeper hits overwrite (matches draw order), so a
            // small button on top of a full-screen panel wins the pick instead of the panel behind it.
            if ( ( forcedRect || hasLayout ) && p.x >= rect.X && p.x <= rect.X + rect.W && p.y >= rect.Y &&
                 p.y <= rect.Y + rect.H )
                hit = e;

            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                std::vector<entt::entity> kids;
                std::vector<Rect>         rects;
                SolveGroupChildren( reg, e, rect, scale, kids, rects );
                if ( !kids.empty() )
                    for ( std::size_t i = 0; i < kids.size(); ++i )
                        PickRecurse( reg, kids[i], rect, scale, p, hit, &rects[i] );
                else
                    for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                        if ( reg.valid( c ) )
                            PickRecurse( reg, c, rect, scale, p, hit );
            }
        }

        void RectRecurse( entt::registry& reg, entt::entity e, const Rect& parent, float scale,
                          entt::entity target, Rect& out, bool& found, const Rect* forcedRect = nullptr )
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
            if ( e == target )
            {
                out   = rect;
                found = true;
                return;
            }
            if ( reg.has<ECS::RelationshipComponent>( e ) )
            {
                std::vector<entt::entity> kids;
                std::vector<Rect>         rects;
                SolveGroupChildren( reg, e, rect, scale, kids, rects );
                if ( !kids.empty() )
                    for ( std::size_t i = 0; i < kids.size() && !found; ++i )
                        RectRecurse( reg, kids[i], rect, scale, target, out, found, &rects[i] );
                else
                    for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                        if ( reg.valid( c ) && !found )
                            RectRecurse( reg, c, rect, scale, target, out, found );
            }
        }
    } // namespace

    entt::entity PickElement( entt::registry& reg, const glm::vec2& pointPx, const Rect& viewportPx )
    {
        entt::entity canvas;
        Rect         canvasRect;
        float        scale = 1.0f;
        if ( !CanvasRootRect( reg, viewportPx, canvas, canvasRect, scale ) )
            return entt::null;

        const auto& cd        = reg.get<ECS::UICanvasComponent>( canvas ).Data;
        const Rect  childRoot = InsetRect( canvasRect, cd.SafeArea.x * scale, cd.SafeArea.y * scale,
                                           cd.SafeArea.z * scale, cd.SafeArea.w * scale );

        entt::entity hit = entt::null;
        if ( reg.has<ECS::RelationshipComponent>( canvas ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvas ).Children )
                if ( reg.valid( c ) )
                    PickRecurse( reg, c, childRoot, scale, pointPx, hit );
        return hit;
    }

    bool GetElementRect( entt::registry& reg, entt::entity target, const Rect& viewportPx, Rect& out )
    {
        entt::entity canvas;
        Rect         canvasRect;
        float        scale = 1.0f;
        if ( !CanvasRootRect( reg, viewportPx, canvas, canvasRect, scale ) )
            return false;

        if ( target == canvas )
        {
            out = canvasRect;
            return true;
        }
        const auto& cd        = reg.get<ECS::UICanvasComponent>( canvas ).Data;
        const Rect  childRoot = InsetRect( canvasRect, cd.SafeArea.x * scale, cd.SafeArea.y * scale,
                                           cd.SafeArea.z * scale, cd.SafeArea.w * scale );
        bool        found     = false;
        if ( reg.has<ECS::RelationshipComponent>( canvas ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvas ).Children )
                if ( reg.valid( c ) && !found )
                    RectRecurse( reg, c, childRoot, scale, target, out, found );
        return found;
    }

    float CanvasScale( entt::registry& reg, const Rect& viewportPx )
    {
        entt::entity canvas;
        Rect         canvasRect;
        float        scale = 1.0f;
        CanvasRootRect( reg, viewportPx, canvas, canvasRect, scale );
        return scale;
    }
} // namespace Desert::UI
