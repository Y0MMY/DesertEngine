// "One view's canvas state never reaches another's."
//
// The defect this exists to prevent, measured on 2026-09-05: every cross-frame value the UI walk kept —
// hover and tween clocks, the elected hot element, the drag, the press edge, the screen stack — lived at
// namespace scope in UICanvasRenderer2D.cpp, one set per process, while the engine draws more than one
// canvas per frame. Three independent ways that bit:
//
//   * the editor builds a Render::RenderRegistry per open scene document, and its constructor installs an
//     EditorUIPass, so two viewports walked two scenes into the same variables;
//   * the UI Editor panel walks the SAME scene a second time and passes input = nullptr, but the walk hands
//     its hot election over at the end whether or not it had input — so the inert preview cleared the
//     viewport's elected element every frame it was open. That one needs no second document;
//   * entt::entity is unique only INSIDE its registry, so the per-entity clocks answered to entity 7 of
//     every scene at once. The same shape as the pipeline-cache key that dropped five fields.
//
// So the assertions here are about the RELATION between two views rather than about either one: two
// registries walked in one frame, and a scene walked twice by two views. Each is written so that giving
// both walks ONE context — which is what the file-scope variables were — turns it red. That mutation was
// run; see the report.
//
// This is also the first test coverage Engine/UI has ever had. scripts/CI/UnreachedSources.sh listed all
// three of its translation units among the 275 that no suite compiles.

#include <Engine/UI/UICanvasContext.hpp>
#include <Engine/UI/UICanvasLayout.hpp>
#include <Engine/UI/UICanvasRenderer2D.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>

// One handle the animated-image stub below answers for, and the fake image it hands back. The draw list
// treats a texture as an OPAQUE id — it stores the pointer and never dereferences it — so a fixed address
// is a complete stand-in for a GPU image here, and it is what lets the canvas-background draw be asserted
// without a device. Only this handle resolves; everything else still gets nothing, so a button with no
// sprite of its own is unaffected.
namespace
{
    constexpr uint64_t kBackgroundHandle = 0xB00B5;

    // Never dereferenced. Taken as an address so it is a real, unique object rather than a made-up number.
    int                       g_FakeImageStorage       = 0;
    bool                      g_BackgroundServiceArmed = false;
    Desert::Graphic::Image2D* FakeImage()
    {
        return reinterpret_cast<Desert::Graphic::Image2D*>( &g_FakeImageStorage );
    }
} // namespace

// The renderer resolves sprites, fonts, icons and video through these. Every one of them owns GPU objects,
// and every draw helper already copes with the service being absent — a sprite that will not resolve falls
// back to its flat colour, text and icons draw nothing. That is exactly the path a headless walk wants, so
// the suite supplies the accessors itself and returns nothing.
namespace Desert::Runtime
{
    TextureService* ResourceRegistry::GetTextureService()
    {
        return nullptr;
    }
    ImageService* ResourceRegistry::GetImageService()
    {
        return nullptr;
    }
    FontService* ResourceRegistry::GetFontService()
    {
        return nullptr;
    }
    IconService* ResourceRegistry::GetIconService()
    {
        return nullptr;
    }
    // The one service the suite can stand up, because the only thing the renderer does with what it
    // returns is put the pointer in a draw command. It is armed by a single test and otherwise absent.
    AnimatedImageService* ResourceRegistry::GetAnimatedImageService()
    {
        static AnimatedImageService stub;
        return g_BackgroundServiceArmed ? &stub : nullptr;
    }
    VideoService* ResourceRegistry::GetVideoService()
    {
        return nullptr;
    }

    // The service METHODS the walk calls on whatever those accessors hand back. Every accessor above
    // returns nullptr, so none of these can run — they exist because the linker still wants the symbols,
    // and each fails the test outright rather than returning a plausible value, so a future change that
    // manages to reach one is a loud failure instead of a quiet stub.
    Graphic::Texture2D* TextureService::Get( const Assets::AssetHandle& ) const
    {
        ADD_FAILURE() << "TextureService::Get reached with no texture service";
        return nullptr;
    }
    Graphic::Image* ImageService::Resolve( const ImageHandle& ) const
    {
        ADD_FAILURE() << "ImageService::Resolve reached with no image service";
        return nullptr;
    }
    // Answers for exactly one handle. Every other sprite in the walk keeps resolving to nothing, so a
    // button or panel with no image of its own draws its flat colour as it does everywhere else.
    Graphic::Image2D* AnimatedImageService::Resolve( const Assets::AssetHandle& handle )
    {
        return static_cast<uint64_t>( handle ) == kBackgroundHandle ? FakeImage() : nullptr;
    }
    Graphic::Image2D* VideoService::Resolve( uint64_t )
    {
        ADD_FAILURE() << "VideoService::Resolve reached with no video service";
        return nullptr;
    }
    Font* FontService::Get( uint64_t, float )
    {
        ADD_FAILURE() << "FontService::Get reached with no font service";
        return nullptr;
    }
    uint64_t FontService::DefaultFontHandle()
    {
        ADD_FAILURE() << "FontService::DefaultFontHandle reached with no font service";
        return 0;
    }
    bool FontService::RequestGlyphs( uint64_t, const std::vector<uint32_t>& )
    {
        ADD_FAILURE() << "FontService::RequestGlyphs reached with no font service";
        return false;
    }
    Icon* IconService::Get( uint64_t )
    {
        ADD_FAILURE() << "IconService::Get reached with no icon service";
        return nullptr;
    }
} // namespace Desert::Runtime

using Desert::UI::Rect;
using Desert::UI::UICanvasContext;
using Desert::UI::UIInput;
namespace ECS = Desert::ECS;
namespace R2D = Desert::Graphic::Render2D;

namespace
{
    constexpr float kSide = 1000.0f; // canvas is Stretch at 1000x1000, so design px == screen px (scale 1)

    const Rect kViewport{ 0.0f, 0.0f, kSide, kSide };

    // A canvas with one button in its top-left corner (0,0)-(100,50). The colours are deliberately far
    // apart in every channel so "which state did it draw" is a exact-equality question, not a threshold.
    struct Fixture
    {
        entt::registry Registry;
        entt::entity   Canvas = entt::null;
        entt::entity   Button = entt::null;

        Fixture()
        {
            Canvas                 = Registry.create();
            auto& canvas           = Registry.emplace<ECS::UICanvasComponent>( Canvas ).Data;
            canvas.ScaleMode       = ECS::UICanvasScaleMode::Stretch;
            canvas.ReferenceWidth  = kSide;
            canvas.ReferenceHeight = kSide;

            Button           = Registry.create();
            auto& layout     = Registry.emplace<ECS::UILayoutComponent>( Button ).Data;
            layout.AnchorMin = { 0.0f, 0.0f };
            layout.AnchorMax = { 0.0f, 0.0f };
            layout.OffsetMin = { 0.0f, 0.0f };
            layout.OffsetMax = { 100.0f, 50.0f };

            auto& button        = Registry.emplace<ECS::UIButtonComponent>( Button ).Data;
            button.NormalColor  = { 0.1f, 0.1f, 0.1f };
            button.HoverColor   = { 0.5f, 0.5f, 0.5f };
            button.PressedColor = { 0.9f, 0.9f, 0.9f };

            Registry.emplace<ECS::RelationshipComponent>( Canvas ).Children.push_back( Button );
            Registry.emplace<ECS::RelationshipComponent>( Button ).Parent = Canvas;
        }
    };

    // Pointer state in canvas pixels. MouseDown makes the drawn colour an exact PressedColor rather than an
    // eased hover mix, which takes the wall clock out of every assertion that only cares about the election.
    UIInput At( float x, float y, bool down = true )
    {
        UIInput in;
        in.MousePx   = { x, y };
        in.MouseDown = down;
        return in;
    }

    // The colour the button was drawn with. It is the only element in the fixture, so the first vertex of
    // the list carries it.
    glm::vec4 DrawnColor( const R2D::DrawList2D& dl )
    {
        EXPECT_FALSE( dl.GetVertices().empty() ) << "the canvas drew nothing at all";
        return dl.GetVertices().empty() ? glm::vec4( -1.0f ) : dl.GetVertices().front().Color;
    }

    // Draw one frame of @p f through @p ctx and hand back what the button was painted.
    glm::vec4 Frame( UICanvasContext& ctx, Fixture& f, const UIInput* input )
    {
        R2D::DrawList2D dl;
        Desert::UI::RenderCanvas2D( ctx, f.Registry, dl, kViewport, /*worldViewProj=*/nullptr, input );
        return DrawnColor( dl );
    }

    // Same, spelled so a call can build the pointer state inline (a temporary lives to the end of the full
    // expression, which is longer than the walk).
    glm::vec4 Frame( UICanvasContext& ctx, Fixture& f, const UIInput& input )
    {
        return Frame( ctx, f, &input );
    }

    // Push this view's wall clock @p seconds into the past, so the NEXT frame it draws measures that delta.
    // The renderer reads a real clock (hover eases and tweens are wall-clock driven by design); this is how
    // a test asks it for a specific one without sleeping.
    void RewindClock( UICanvasContext& ctx, float seconds )
    {
        ctx.LastFrameTime -= seconds;
    }

    bool SameColor( const glm::vec4& a, const glm::vec3& rgb )
    {
        return std::fabs( a.r - rgb.r ) < 1e-5f && std::fabs( a.g - rgb.g ) < 1e-5f &&
               std::fabs( a.b - rgb.b ) < 1e-5f;
    }
} // namespace

// --- (1) Two scenes, two views, one frame ----------------------------------------------------------------
//
// The editor case. Both registries hand out the SAME entity ids — that is the point, and it is why the key
// had to stop being a bare entt::entity.
TEST( UICanvasContext, TheHotElectionOfOneViewDoesNotReachAnother )
{
    Fixture a, b;
    ASSERT_EQ( a.Button, b.Button ) << "the two registries must hand out the same id for this to test anything";

    UICanvasContext ctxA, ctxB;

    // Frame 1 elects: A's pointer is on its button, B's is far away. Controls react to the PREVIOUS frame's
    // winner, so nothing is pressed yet in either.
    Frame( ctxA, a, At( 10.0f, 10.0f ) );
    Frame( ctxB, b, At( 900.0f, 900.0f ) );

    // Frame 2 acts on that election.
    const glm::vec4 drawnA = Frame( ctxA, a, At( 10.0f, 10.0f ) );
    const glm::vec4 drawnB = Frame( ctxB, b, At( 900.0f, 900.0f ) );

    EXPECT_TRUE( SameColor( drawnA, glm::vec3( 0.9f ) ) )
         << "the pointer is inside A's button and it did not react";
    EXPECT_TRUE( SameColor( drawnB, glm::vec3( 0.1f ) ) )
         << "B's pointer is 900 px away from its button, but the button lit up — A's election reached it";
}

// --- (2) One scene, two views, and the second one has no input at all ------------------------------------
//
// The UI Editor panel. Inertness is not enough: the hand-over at the end of the walk (Hot = HotNext) runs
// whether or not there was input, so a second inert walk over the same scene used to null the viewport's
// elected element every frame. Observable with one document open, which is what made it the third argument.
TEST( UICanvasContext, AnInertPreviewDoesNotClearTheInteractiveViewsElection )
{
    Fixture         f;
    UICanvasContext viewport;
    UICanvasContext preview;
    preview.DrivesSceneAnimation = false; // as UIEditorPanel configures it

    Frame( viewport, f, At( 10.0f, 10.0f ) );
    Frame( preview, f, nullptr ); // the authoring window, drawn in the same frame

    const glm::vec4 drawn = Frame( viewport, f, At( 10.0f, 10.0f ) );
    EXPECT_TRUE( SameColor( drawn, glm::vec3( 0.9f ) ) )
         << "the viewport's button stopped reacting while an inert preview of the same scene was drawn";
}

// --- (3) The per-entity key ------------------------------------------------------------------------------
//
// A hover clock stored against a bare entt::entity is a key two scenes both answer to. Here view A has fully
// hovered ITS entity 1; view B's entity 1 is a different button in a different registry and must be at rest.
TEST( UICanvasContext, APerEntityClockIsKeyedInsideItsOwnView )
{
    Fixture         a, b;
    UICanvasContext ctxA, ctxB;

    Frame( ctxA, a, At( 10.0f, 10.0f, /*down=*/false ) );
    Frame( ctxB, b, At( 900.0f, 900.0f, /*down=*/false ) );

    // A has been hovering long enough for its ease to saturate.
    ctxA.HoverT[a.Button] = 1.0f;
    ASSERT_EQ( ctxA.HoverT.count( a.Button ), 1u );

    RewindClock( ctxB, 0.5f ); // give B a real frame delta, so a leaked clock would have time to show
    const glm::vec4 drawnB = Frame( ctxB, b, At( 900.0f, 900.0f, /*down=*/false ) );

    EXPECT_TRUE( SameColor( drawnB, glm::vec3( 0.1f ) ) )
         << "B's button drew a hover blend from a clock that belongs to A's entity of the same id";
    EXPECT_NEAR( ctxB.HoverT[b.Button], 0.0f, 1e-4f );
}

// --- (4) Each view keeps its own frame delta -------------------------------------------------------------
//
// The clock was one file-scope float refreshed at the top of every call, so of two walks in one frame the
// second measured ~0 seconds and its hover eases, tweens and screen transition stood still. Both views here
// are handed the same 50 ms and must both spend it.
TEST( UICanvasContext, EveryViewMeasuresItsOwnFrameDelta )
{
    Fixture         a, b;
    UICanvasContext ctxA, ctxB;

    Frame( ctxA, a, At( 10.0f, 10.0f, /*down=*/false ) ); // seed both clocks
    Frame( ctxB, b, At( 10.0f, 10.0f, /*down=*/false ) );

    RewindClock( ctxA, 0.05f );
    RewindClock( ctxB, 0.05f );
    Frame( ctxA, a, At( 10.0f, 10.0f, /*down=*/false ) );
    Frame( ctxB, b, At( 10.0f, 10.0f, /*down=*/false ) );

    EXPECT_NEAR( ctxA.FrameDt, 0.05f, 5e-3f );
    EXPECT_NEAR( ctxB.FrameDt, 0.05f, 5e-3f )
         << "the second view of the frame measured no time — the two walks are sharing one clock";

    // And the hover ease that delta drives moved by the same amount in both. The tolerances here are wide
    // on purpose: the clock is a real one, the two walks are microseconds apart, and the defect this
    // catches is one view easing to 0.6 while the other sits at exactly 0 — not a difference in the fourth
    // decimal. A tighter bound made this test fail on the spread between two consecutive steady_clock
    // reads, which is a flake and worse than no test at all.
    EXPECT_GT( ctxA.HoverT[a.Button], 0.5f ) << "50 ms of hover moved view A's ease by nothing";
    EXPECT_GT( ctxB.HoverT[b.Button], 0.5f ) << "50 ms of hover moved view B's ease by nothing";
    EXPECT_NEAR( ctxA.HoverT[a.Button], ctxB.HoverT[b.Button], 0.01f );
}

// --- (5) Screen navigation is view state, the anim playhead is scene state -------------------------------
//
// Two views of one scene: one navigates, the other must not follow. This is the half of the split that had
// to stay OUT of the components (UI_ROADMAP.md section F) — navigating in the editor must not rewrite the
// authored scene.
TEST( UICanvasContext, ScreenNavigationBelongsToTheViewThatDidIt )
{
    Fixture f;

    // Rehome the button under a "Home" screen and add an empty "Settings" beside it, which is how a real
    // canvas with pages is built. InitialScreen is named rather than left to the seeding loop's first hit:
    // that loop walks an entt view, whose order is the component pool's, not the creation order.
    auto& stack         = f.Registry.emplace<ECS::UIScreenStackComponent>( f.Canvas ).Data;
    stack.InitialScreen = "Home";

    const entt::entity home                                      = f.Registry.create();
    f.Registry.emplace<ECS::UIScreenComponent>( home ).Data.Name = "Home";
    auto& homeLayout     = f.Registry.emplace<ECS::UILayoutComponent>( home ).Data;
    homeLayout.AnchorMax = { 1.0f, 1.0f }; // a screen spreads over the whole canvas
    homeLayout.OffsetMax = { 0.0f, 0.0f };

    const entt::entity settings                                      = f.Registry.create();
    f.Registry.emplace<ECS::UIScreenComponent>( settings ).Data.Name = "Settings";
    auto& settingsLayout     = f.Registry.emplace<ECS::UILayoutComponent>( settings ).Data;
    settingsLayout.AnchorMax = { 1.0f, 1.0f };
    settingsLayout.OffsetMax = { 0.0f, 0.0f };

    auto& canvasKids = f.Registry.get<ECS::RelationshipComponent>( f.Canvas ).Children;
    canvasKids.clear();
    canvasKids.push_back( home );
    canvasKids.push_back( settings );
    f.Registry.emplace<ECS::RelationshipComponent>( home ).Children.push_back( f.Button );
    f.Registry.emplace<ECS::RelationshipComponent>( settings );
    f.Registry.get<ECS::RelationshipComponent>( f.Button ).Parent = home;

    auto& button          = f.Registry.get<ECS::UIButtonComponent>( f.Button ).Data;
    button.Action         = ECS::UIButtonAction::ShowScreen;
    button.OnClickMessage = "Settings";

    UICanvasContext viewport, second;

    // Seed both views, then release the pointer over the button in ONE of them.
    Frame( viewport, f, At( 10.0f, 10.0f ) );
    Frame( second, f, At( 900.0f, 900.0f ) );
    ASSERT_TRUE( viewport.Hot == f.Button ) << "the pointer sat on the button and something else was elected";

    UIInput click       = At( 10.0f, 10.0f, /*down=*/false );
    click.MouseReleased = true;
    {
        R2D::DrawList2D dl;
        std::string     clicked;
        Desert::UI::RenderCanvas2D( viewport, f.Registry, dl, kViewport, nullptr, &click, &clicked );
        EXPECT_EQ( clicked, "screen:Settings" );
    }
    Frame( second, f, At( 900.0f, 900.0f ) );

    EXPECT_EQ( viewport.Screen, "Settings" );
    EXPECT_EQ( second.Screen, "Home" ) << "a second view of the same scene followed a navigation it never made";
}

// --- (6) The one clock that is NOT view state ------------------------------------------------------------
//
// UIAnimComponent's playhead lives in the component because the Sequencer scrubs it, so it is SCENE state
// and exactly one view may advance it. Both advancing it is the mirror image of the bug this whole change
// fixes: every clip would run at twice its authored speed whenever the UI Editor panel is open.
TEST( UICanvasContext, OnlyTheDrivingViewAdvancesTheScenesAnimationPlayhead )
{
    Fixture f;
    auto&   clip  = f.Registry.emplace<ECS::UIAnimComponent>( f.Button ).Data;
    clip.Playing  = true;
    clip.Duration = 100.0f; // long enough that nothing wraps
    clip.Loop     = false;

    UICanvasContext viewport;
    UICanvasContext preview;
    preview.DrivesSceneAnimation = false;

    Frame( viewport, f, At( 900.0f, 900.0f, /*down=*/false ) );
    Frame( preview, f, nullptr );
    clip.Time = 5.0f;

    RewindClock( preview, 0.05f );
    Frame( preview, f, nullptr );
    EXPECT_FLOAT_EQ( clip.Time, 5.0f ) << "the authoring preview advanced a playhead it does not own";

    RewindClock( viewport, 0.05f );
    Frame( viewport, f, At( 900.0f, 900.0f, /*down=*/false ) );
    EXPECT_NEAR( clip.Time, 5.05f, 5e-3f ) << "the driving view did not advance the playhead";
}

// --- (7) A view pointed at another scene forgets the first one -------------------------------------------
//
// One host does reuse its context across scenes: the UI Editor panel follows the active document. The ids
// it remembers mean something else in the new registry, so the context drops them.
TEST( UICanvasContext, RebindingAViewToAnotherRegistryDropsItsPerEntityState )
{
    Fixture         a, b;
    UICanvasContext ctx;

    Frame( ctx, a, At( 10.0f, 10.0f ) );
    Frame( ctx, a, At( 10.0f, 10.0f ) );
    ASSERT_EQ( ctx.Hot, a.Button ) << "the pointer was over A's button for two frames and it was not elected";
    ASSERT_FALSE( ctx.HoverT.empty() );

    const glm::vec4 drawnB = Frame( ctx, b, At( 900.0f, 900.0f ) );
    EXPECT_TRUE( ctx.Hot == entt::null ) << "the election survived a change of scene";
    EXPECT_TRUE( SameColor( drawnB, glm::vec3( 0.1f ) ) )
         << "B's button reacted to an election made in A, because the id matched";
}

// --- (8) The canvas background must not invent a colour it does not have ---------------------------------
//
// UICanvasData::Sprite was a dead setting — reflected, serialized, shown in Details, read by nothing. It now
// draws as a full-canvas backdrop. It has no colour of its own, so it may NOT take the flat-fill fallback a
// panel takes: doing so paints an opaque white sheet over the whole scene whenever the image is missing.
// Here no image service exists, so nothing can resolve, and the frame must be exactly what it was before.
TEST( UICanvasContext, AnUnresolvableCanvasBackgroundDrawsNothingRatherThanAWhiteSheet )
{
    Fixture bare, withSprite;
    withSprite.Registry.get<ECS::UICanvasComponent>( withSprite.Canvas ).Data.Sprite =
         Desert::Assets::AssetHandle( 0x1234u );

    UICanvasContext c1, c2;
    R2D::DrawList2D dlBare, dlSprite;
    Desert::UI::RenderCanvas2D( c1, bare.Registry, dlBare, kViewport );
    Desert::UI::RenderCanvas2D( c2, withSprite.Registry, dlSprite, kViewport );

    EXPECT_EQ( dlSprite.GetVertices().size(), dlBare.GetVertices().size() )
         << "a background sprite that did not resolve still put geometry on screen";
    EXPECT_TRUE( SameColor( DrawnColor( dlSprite ), glm::vec3( 0.1f ) ) )
         << "the first thing drawn is no longer the button — a backdrop was painted under it from nothing";
}

// --- (9) And when it DOES resolve, it is drawn: full canvas, under everything ----------------------------
//
// The other half of the dead setting. Test (8) says a background that cannot resolve invents nothing; this
// one says a background that can resolve reaches the draw list, covers the whole canvas rect, and is the
// FIRST thing emitted so every child lands on top of it.
//
// It is asserted here rather than in a frame because a canvas background cannot currently be authored in a
// .desce at all — see the report: TextureAsset handles serialize as an absolute machine-local path, the
// read side does not create-on-miss and returns 0 without a log, and a numeric handle above 2^53 is mangled
// by the JSON double round-trip (measured: 5355760296319878840 came back as 5355760296319879168). All three
// live in the scene serializer, none of them in this task's files.
TEST( UICanvasContext, AResolvableCanvasBackgroundCoversTheCanvasAndIsDrawnFirst )
{
    Fixture f;
    f.Registry.get<ECS::UICanvasComponent>( f.Canvas ).Data.Sprite =
         Desert::Assets::AssetHandle( kBackgroundHandle );

    g_BackgroundServiceArmed = true;
    UICanvasContext ctx;
    R2D::DrawList2D dl;
    Desert::UI::RenderCanvas2D( ctx, f.Registry, dl, kViewport );
    g_BackgroundServiceArmed = false;

    ASSERT_FALSE( dl.GetCommands().empty() );
    EXPECT_EQ( dl.GetCommands().front().Texture, FakeImage() )
         << "the first draw command is not the canvas backdrop, so a child would be painted over by it";

    // The first quad is the backdrop: four vertices spanning the whole canvas, which at Stretch is the
    // whole viewport. The safe area does not cut it -- a notch inset says where CONTENT may not go, not
    // where the wallpaper stops.
    ASSERT_GE( dl.GetVertices().size(), 4u );
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for ( std::size_t i = 0; i < 4; ++i )
    {
        const glm::vec2 p = dl.GetVertices()[i].Position;
        minX              = std::min( minX, p.x );
        minY              = std::min( minY, p.y );
        maxX              = std::max( maxX, p.x );
        maxY              = std::max( maxY, p.y );
    }
    EXPECT_FLOAT_EQ( minX, 0.0f );
    EXPECT_FLOAT_EQ( minY, 0.0f );
    EXPECT_FLOAT_EQ( maxX, kSide );
    EXPECT_FLOAT_EQ( maxY, kSide );

    // And the button is still drawn, on top: the backdrop did not replace the tree.
    EXPECT_GT( dl.GetVertices().size(), 4u ) << "the canvas drew its backdrop and nothing else";
}

// =========================================================================================================
// У4 — the two visibility axes. Written as RELATIONS between two arrangements rather than as expected
// pixel coordinates, because the numbers a layout group produces are not the claim: the claim is that
// Collapsed costs its neighbours exactly one slot and Hidden costs them nothing.
// =========================================================================================================

namespace
{
    // A VBox filling the canvas with three 100x50 items stacked top to bottom, no spacing and no padding,
    // so a slot is worth exactly its own height and the arithmetic below has no other term in it. The
    // three colours are far apart in every channel so a rect can be recovered from the vertex buffer by
    // colour, which survives corner rounding and any other geometry the panel decides to emit.
    struct Stack
    {
        static constexpr float kItemH = 50.0f;

        entt::registry Registry;
        entt::entity   Canvas = entt::null;
        entt::entity   Box    = entt::null;
        entt::entity   Item[3]{ entt::null, entt::null, entt::null };

        static glm::vec3 ColorOf( int i )
        {
            return i == 0 ? glm::vec3( 1.0f, 0.0f, 0.0f )
                   : i == 1 ? glm::vec3( 0.0f, 1.0f, 0.0f )
                            : glm::vec3( 0.0f, 0.0f, 1.0f );
        }

        Stack()
        {
            Canvas                 = Registry.create();
            auto& canvas           = Registry.emplace<ECS::UICanvasComponent>( Canvas ).Data;
            canvas.ScaleMode       = ECS::UICanvasScaleMode::Stretch;
            canvas.ReferenceWidth  = kSide;
            canvas.ReferenceHeight = kSide;

            Box                = Registry.create();
            auto& boxLayout    = Registry.emplace<ECS::UILayoutComponent>( Box ).Data;
            boxLayout.AnchorMin = { 0.0f, 0.0f };
            boxLayout.AnchorMax = { 1.0f, 1.0f };
            boxLayout.OffsetMin = { 0.0f, 0.0f };
            boxLayout.OffsetMax = { 0.0f, 0.0f };

            auto& group        = Registry.emplace<ECS::UILayoutGroupComponent>( Box ).Data;
            group.Type         = ECS::UILayoutType::Vertical;
            group.Spacing      = 0.0f;
            group.Padding      = glm::vec4( 0.0f );
            group.StretchCross = true;

            Registry.emplace<ECS::RelationshipComponent>( Canvas ).Children.push_back( Box );
            Registry.emplace<ECS::RelationshipComponent>( Box ).Parent = Canvas;

            // Every entity is created and given its components BEFORE any of them is linked up. Holding a
            // reference into a component pool across a later emplace into that same pool is a dangling
            // one — entt is free to reallocate — and the first version of this fixture did exactly that:
            // the box's children vector was written through a freed pointer and the stack drew nothing.
            for ( int i = 0; i < 3; ++i )
            {
                Item[i]          = Registry.create();
                auto& layout     = Registry.emplace<ECS::UILayoutComponent>( Item[i] ).Data;
                layout.AnchorMin = { 0.0f, 0.0f };
                layout.AnchorMax = { 0.0f, 0.0f };
                layout.OffsetMin = { 0.0f, 0.0f };
                layout.OffsetMax = { 100.0f, kItemH }; // preferred size = the slot the group gives it

                auto& panel        = Registry.emplace<ECS::UIPanelComponent>( Item[i] ).Data;
                panel.Color        = ColorOf( i );
                panel.Opacity      = 1.0f;
                panel.CornerRadius = 0.0f;

                Registry.emplace<ECS::RelationshipComponent>( Item[i] ).Parent = Box;
            }
            for ( int i = 0; i < 3; ++i )
                Registry.get<ECS::RelationshipComponent>( Box ).Children.push_back( Item[i] );
        }

        void SetVisibility( int item, ECS::UIVisibility v )
        {
            Registry.get<ECS::UILayoutComponent>( Item[item] ).Data.Visibility = v;
        }
    };

    // The bounding box of every vertex painted in @p rgb, or nullopt when the colour was never drawn. This
    // is how "where did that element end up" is read back without asking the renderer to report it.
    std::optional<Rect> RectOfColor( const R2D::DrawList2D& dl, const glm::vec3& rgb )
    {
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        bool  seen = false;
        for ( const auto& v : dl.GetVertices() )
        {
            if ( !SameColor( v.Color, rgb ) )
                continue;
            seen = true;
            minX = std::min( minX, v.Position.x );
            minY = std::min( minY, v.Position.y );
            maxX = std::max( maxX, v.Position.x );
            maxY = std::max( maxY, v.Position.y );
        }
        if ( !seen )
            return std::nullopt;
        return Rect{ minX, minY, maxX - minX, maxY - minY };
    }

    // Draw @p s once and hand back where each of its three items landed (nullopt = not drawn at all).
    std::array<std::optional<Rect>, 3> Layout( Stack& s )
    {
        UICanvasContext ctx;
        R2D::DrawList2D dl;
        Desert::UI::RenderCanvas2D( ctx, s.Registry, dl, kViewport );
        return { RectOfColor( dl, Stack::ColorOf( 0 ) ), RectOfColor( dl, Stack::ColorOf( 1 ) ),
                 RectOfColor( dl, Stack::ColorOf( 2 ) ) };
    }
} // namespace

// --- (10) THE LAYOUT AXIS -------------------------------------------------------------------------------
//
// The relation, and it is one subtraction: a Collapsed element costs the siblings below it exactly its own
// slot, and a Hidden one costs them nothing. Asserting the three absolute positions instead would pass just
// as happily on a build where Hidden also closed the gap, as long as the arithmetic was self-consistent.
TEST( UICanvasVisibility, CollapsedCostsTheSiblingsExactlyOneSlotAndHiddenCostsThemNothing )
{
    Stack visible, hidden, collapsed;
    hidden.SetVisibility( 1, ECS::UIVisibility::Hidden );
    collapsed.SetVisibility( 1, ECS::UIVisibility::Collapsed );

    const auto v = Layout( visible );
    const auto h = Layout( hidden );
    const auto c = Layout( collapsed );

    ASSERT_TRUE( v[0] && v[1] && v[2] ) << "the untouched stack did not draw all three items";
    ASSERT_TRUE( h[0] && h[2] );
    ASSERT_TRUE( c[0] && c[2] );

    // Neither state draws the element. That is the half the two share.
    EXPECT_FALSE( h[1].has_value() ) << "a Hidden element was still painted";
    EXPECT_FALSE( c[1].has_value() ) << "a Collapsed element was still painted";

    // The first item is above the change and must not move in either.
    EXPECT_FLOAT_EQ( h[0]->Y, v[0]->Y );
    EXPECT_FLOAT_EQ( c[0]->Y, v[0]->Y );

    // THE RELATION. The gap the middle item held is exactly its own height, so the item below it moves up
    // by that and by nothing else when it collapses, and does not move at all when it merely hides.
    EXPECT_FLOAT_EQ( h[2]->Y, v[2]->Y ) << "Hidden closed the gap — then it is Collapsed under another name";
    EXPECT_FLOAT_EQ( v[2]->Y - c[2]->Y, Stack::kItemH )
         << "Collapsed moved the sibling below by " << ( v[2]->Y - c[2]->Y ) << " px and the slot it "
         << "vacated is " << Stack::kItemH << " px tall";

    // And the surviving items keep their size: a closed gap redistributes position, not height.
    EXPECT_FLOAT_EQ( c[2]->H, v[2]->H );
    EXPECT_FLOAT_EQ( h[2]->H, v[2]->H );
}

// --- (11) The pick and the draw are one layout ----------------------------------------------------------
//
// The editor resolves rects a second time (UICanvasLayout.cpp) so a click in the viewport selects what is
// under it. Two solvers that must agree is the defect shape this project keeps hitting, and Collapsed is a
// fresh chance to hit it: if the pick still gave the collapsed child a slot, every element below it would
// be selectable 50 px away from where it is drawn.
TEST( UICanvasVisibility, TheEditorPickAgreesWithTheDrawAboutACollapsedSlot )
{
    Stack s;
    s.SetVisibility( 1, ECS::UIVisibility::Collapsed );

    const auto drawn = Layout( s );
    ASSERT_TRUE( drawn[2].has_value() );

    const glm::vec2 inside( drawn[2]->X + 5.0f, drawn[2]->Y + drawn[2]->H * 0.5f );
    EXPECT_EQ( Desert::UI::PickElement( s.Registry, inside, kViewport ), s.Item[2] )
         << "a click in the middle of the third item, where it is DRAWN, did not pick it";

    // And the collapsed one is not pickable anywhere, because it is nowhere.
    for ( float y = 0.0f; y < 3.0f * Stack::kItemH; y += 5.0f )
        EXPECT_NE( Desert::UI::PickElement( s.Registry, { 5.0f, y }, kViewport ), s.Item[1] )
             << "the collapsed item was picked at y=" << y;
}

// =========================================================================================================
// The hit-test axis. All four values are asserted through the ELECTION (ctx.Hot), because that is the one
// thing every control downstream reads, and through whether the button reacts, because being elected and
// responding are the two halves the four values split differently.
// =========================================================================================================

namespace
{
    // A full-canvas panel with a button in its top-left corner. The panel is the ancestor whose HitTest is
    // under test; the button is what the pointer is really over.
    struct Nested
    {
        entt::registry Registry;
        entt::entity   Canvas = entt::null;
        entt::entity   Panel  = entt::null;
        entt::entity   Button = entt::null;

        Nested()
        {
            Canvas                 = Registry.create();
            auto& canvas           = Registry.emplace<ECS::UICanvasComponent>( Canvas ).Data;
            canvas.ScaleMode       = ECS::UICanvasScaleMode::Stretch;
            canvas.ReferenceWidth  = kSide;
            canvas.ReferenceHeight = kSide;

            Panel                = Registry.create();
            auto& panelLayout    = Registry.emplace<ECS::UILayoutComponent>( Panel ).Data;
            panelLayout.AnchorMin = { 0.0f, 0.0f };
            panelLayout.AnchorMax = { 1.0f, 1.0f };
            panelLayout.OffsetMin = { 0.0f, 0.0f };
            panelLayout.OffsetMax = { 0.0f, 0.0f };
            Registry.emplace<ECS::UIPanelComponent>( Panel );

            Button           = Registry.create();
            auto& layout     = Registry.emplace<ECS::UILayoutComponent>( Button ).Data;
            layout.AnchorMin = { 0.0f, 0.0f };
            layout.AnchorMax = { 0.0f, 0.0f };
            layout.OffsetMin = { 0.0f, 0.0f };
            layout.OffsetMax = { 100.0f, 50.0f };

            auto& button        = Registry.emplace<ECS::UIButtonComponent>( Button ).Data;
            button.NormalColor  = { 0.1f, 0.1f, 0.1f };
            button.HoverColor   = { 0.5f, 0.5f, 0.5f };
            button.PressedColor = { 0.9f, 0.9f, 0.9f };

            Registry.emplace<ECS::RelationshipComponent>( Canvas ).Children.push_back( Panel );
            auto& panelKids  = Registry.emplace<ECS::RelationshipComponent>( Panel );
            panelKids.Parent = Canvas;
            panelKids.Children.push_back( Button );
            Registry.emplace<ECS::RelationshipComponent>( Button ).Parent = Panel;
        }

        void SetHitTest( entt::entity e, ECS::UIHitTest h )
        {
            Registry.get<ECS::UILayoutComponent>( e ).Data.HitTest = h;
        }
    };

    // Two frames of @p n with the pointer held at (@p x, @p y): the first elects, the second acts on that
    // election (controls compare against the PREVIOUS frame's winner). Hands back who was elected and what
    // the button was painted.
    struct Probe
    {
        entt::entity Hot = entt::null;
        glm::vec4    ButtonColor{ -1.0f };
    };

    Probe Press( Nested& n, float x, float y )
    {
        UICanvasContext ctx;
        R2D::DrawList2D first;
        const UIInput   in = At( x, y );
        Desert::UI::RenderCanvas2D( ctx, n.Registry, first, kViewport, nullptr, &in );

        Probe           out;
        R2D::DrawList2D second;
        Desert::UI::RenderCanvas2D( ctx, n.Registry, second, kViewport, nullptr, &in );
        out.Hot = ctx.HotNext == entt::null ? ctx.Hot : ctx.HotNext;
        // The panel is drawn first and the button on top of it, so the button's quad is the LAST colour in
        // the list that is one of its three states.
        for ( const auto& v : second.GetVertices() )
            if ( SameColor( v.Color, glm::vec3( 0.1f ) ) || SameColor( v.Color, glm::vec3( 0.5f ) ) ||
                 SameColor( v.Color, glm::vec3( 0.9f ) ) )
                out.ButtonColor = v.Color;
        return out;
    }
} // namespace

// --- (12) All: the baseline both ways -------------------------------------------------------------------
TEST( UICanvasHitTest, AllElectsTheElementAndItsChildren )
{
    Nested n;

    const Probe onButton = Press( n, 10.0f, 10.0f );
    EXPECT_EQ( onButton.Hot, n.Button );
    EXPECT_TRUE( SameColor( onButton.ButtonColor, glm::vec3( 0.9f ) ) ) << "the button did not react";

    const Probe onPanel = Press( n, 900.0f, 900.0f );
    EXPECT_EQ( onPanel.Hot, n.Panel ) << "a plain panel must stop the pointer; that is what All means";
}

// --- (13) ChildrenOnly: the old RaycastTarget = false ---------------------------------------------------
//
// The two halves in one test, because either alone is satisfied by a mistake: the element must NOT be
// elected where only it is under the pointer, and its child must STILL be elected where the child is.
TEST( UICanvasHitTest, ChildrenOnlyDoesNotElectItselfButStillElectsItsChild )
{
    Nested n;
    n.SetHitTest( n.Panel, ECS::UIHitTest::ChildrenOnly );

    EXPECT_TRUE( Press( n, 900.0f, 900.0f ).Hot == entt::null )
         << "a ChildrenOnly element was elected where nothing but it is under the pointer";

    const Probe onButton = Press( n, 10.0f, 10.0f );
    EXPECT_EQ( onButton.Hot, n.Button ) << "the child of a transparent parent stopped being hit-testable";
    EXPECT_TRUE( SameColor( onButton.ButtonColor, glm::vec3( 0.9f ) ) )
         << "the child was elected but no longer responds";
}

// --- (14) None: UE's HitTestInvisible, which neither old boolean could say ------------------------------
//
// THE SUB-TREE IS THE POINT. RaycastTarget = false cleared on the panel alone left the button underneath
// perfectly clickable, so "this overlay lets every click through" had to be spelled by clearing a flag on
// every descendant by hand. Nothing anywhere under a None may be elected.
TEST( UICanvasHitTest, NothingInTheSubTreeOfANoneCanBecomeHot )
{
    Nested n;
    n.SetHitTest( n.Panel, ECS::UIHitTest::None );

    EXPECT_TRUE( Press( n, 900.0f, 900.0f ).Hot == entt::null );

    const Probe onButton = Press( n, 10.0f, 10.0f );
    EXPECT_TRUE( onButton.Hot == entt::null ) << "the button under a HitTest::None panel was still elected";
    EXPECT_TRUE( SameColor( onButton.ButtonColor, glm::vec3( 0.1f ) ) )
         << "the button under a HitTest::None panel still reacted to the pointer";

    // And it is the ANCESTOR's value doing it: the button's own is untouched and says All.
    EXPECT_EQ( n.Registry.get<ECS::UILayoutComponent>( n.Button ).Data.HitTest, ECS::UIHitTest::All );
}

// --- (15) Blocking: what Interactable = false became, plus the propagation it never had ----------------
//
// Two claims that pull in opposite directions and are both required: the element STOPS the pointer (a modal
// scrim has to swallow the click) and NOTHING under it responds (a greyed-out form is grey all the way
// down). A value that only did the first would be All; one that only did the second would be None.
TEST( UICanvasHitTest, BlockingStopsThePointerAndSilencesTheWholeSubTree )
{
    Nested n;
    n.SetHitTest( n.Panel, ECS::UIHitTest::Blocking );

    EXPECT_EQ( Press( n, 900.0f, 900.0f ).Hot, n.Panel ) << "a Blocking element let the pointer past it";

    const Probe onButton = Press( n, 10.0f, 10.0f );
    EXPECT_EQ( onButton.Hot, n.Panel )
         << "the click landed on the button inside a Blocking panel instead of being swallowed by it";
    EXPECT_TRUE( SameColor( onButton.ButtonColor, glm::vec3( 0.1f ) ) )
         << "a button inside a Blocking panel still reacted — the old Interactable flag did not propagate "
            "and this value exists to fix exactly that";
}

// --- (16) The axes do not leak into each other ---------------------------------------------------------
//
// Nine of the twelve products are Hidden or Collapsed, where the hit-test value cannot be observed because
// there is nothing on screen to point at. That is a property to STATE, not to leave implied: an element
// nobody can see must not eat clicks whatever its Hit Test says, which is also UE's rule (neither Hidden
// nor Collapsed is hit-testable there either).
TEST( UICanvasHitTest, AnElementThatIsNotVisibleIsNotHitTestableWhateverItsHitTestSays )
{
    for ( const ECS::UIVisibility invisible : { ECS::UIVisibility::Hidden, ECS::UIVisibility::Collapsed } )
    {
        for ( const ECS::UIHitTest hit : { ECS::UIHitTest::All, ECS::UIHitTest::ChildrenOnly,
                                           ECS::UIHitTest::Blocking, ECS::UIHitTest::None } )
        {
            Nested n;
            n.Registry.get<ECS::UILayoutComponent>( n.Panel ).Data.Visibility = invisible;
            n.SetHitTest( n.Panel, hit );

            EXPECT_TRUE( Press( n, 900.0f, 900.0f ).Hot == entt::null )
                 << "an invisible panel was elected with Visibility " << static_cast<int>( invisible )
                 << " and Hit Test " << static_cast<int>( hit );
            EXPECT_TRUE( Press( n, 10.0f, 10.0f ).Hot == entt::null )
                 << "the button inside an invisible panel was elected with Visibility "
                 << static_cast<int>( invisible ) << " and Hit Test " << static_cast<int>( hit );
        }
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
