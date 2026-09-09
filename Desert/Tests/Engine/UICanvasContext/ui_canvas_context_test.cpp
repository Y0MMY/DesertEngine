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

    // Every walk in this file goes through here. RenderCanvas2D REFUSES rather than returning a bare false
    // (Ю1), and a test that swallowed the refusal would go on to assert about an empty draw list and pass
    // for entirely the wrong reason — so the refusal is surfaced at the one place that makes the call.
    bool Draw( UICanvasContext& ctx, entt::registry& reg, entt::entity canvas, R2D::DrawList2D& dl,
               const UIInput* input = nullptr, std::string* outClicked = nullptr, entt::entity* focused = nullptr,
               std::vector<std::string>* outMessages = nullptr )
    {
        const auto drawn =
             Desert::UI::RenderCanvas2D( ctx, reg, canvas, dl, kViewport,
                                         /*worldViewProj=*/nullptr, input, outClicked, focused, outMessages );
        EXPECT_TRUE( drawn.IsSuccess() ) << drawn.GetError();
        return drawn.IsSuccess() && drawn.GetValue();
    }

    // Draw one frame of @p f through @p ctx and hand back what the button was painted.
    glm::vec4 Frame( UICanvasContext& ctx, Fixture& f, const UIInput* input )
    {
        R2D::DrawList2D dl;
        Draw( ctx, f.Registry, f.Canvas, dl, input );
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
        Draw( viewport, f.Registry, f.Canvas, dl, &click, &clicked );
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
    Draw( c1, bare.Registry, bare.Canvas, dlBare );
    Draw( c2, withSprite.Registry, withSprite.Canvas, dlSprite );

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
// It is asserted here rather than in a frame because RenderCanvas2D takes a registry and a draw list and
// touches no file.
//
// THIS COMMENT USED TO SAY A CANVAS BACKGROUND COULD NOT BE AUTHORED IN A `.desce` AT ALL, and listed
// three serializer defects behind that: an absolute machine-local path on the write side, a silent 0 with
// no log on the read side, and a numeric handle above 2^53 mangled by a JSON double round trip (measured:
// 5355760296319878840 came back as 5355760296319879168). All three were real and ALL THREE ARE FIXED — the
// first two by Ф5's extraction of the texture reference into Engine/Core/Serialize/TextureSlot.cpp (which
// stores the root-tagged stable key and logs every miss with the roots it searched), the third by
// ReflectionSerializer's integral read path. Each has its own suite now: TextureSlotRoundTrip,
// ReflectionSerializer and UIComponentRoundTrip, the last of which round-trips THIS component's Sprite
// through JSON text on the very handle quoted above. `Editor/Resources/Assets/Scenes/UI_SpriteSlots.desce`
// carries an authored canvas background as `cooked:Textures/T_Checker.tex`, which is the same claim made
// in the corpus rather than in a comment.
TEST( UICanvasContext, AResolvableCanvasBackgroundCoversTheCanvasAndIsDrawnFirst )
{
    Fixture f;
    f.Registry.get<ECS::UICanvasComponent>( f.Canvas ).Data.Sprite =
         Desert::Assets::AssetHandle( kBackgroundHandle );

    g_BackgroundServiceArmed = true;
    UICanvasContext ctx;
    R2D::DrawList2D dl;
    Draw( ctx, f.Registry, f.Canvas, dl );
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
            return i == 0   ? glm::vec3( 1.0f, 0.0f, 0.0f )
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

            Box                 = Registry.create();
            auto& boxLayout     = Registry.emplace<ECS::UILayoutComponent>( Box ).Data;
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
        Draw( ctx, s.Registry, s.Canvas, dl );
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
    EXPECT_EQ( Desert::UI::PickElement( s.Registry, s.Canvas, inside, kViewport ), s.Item[2] )
         << "a click in the middle of the third item, where it is DRAWN, did not pick it";

    // And the collapsed one is not pickable anywhere, because it is nowhere.
    for ( float y = 0.0f; y < 3.0f * Stack::kItemH; y += 5.0f )
        EXPECT_NE( Desert::UI::PickElement( s.Registry, s.Canvas, { 5.0f, y }, kViewport ), s.Item[1] )
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

            Panel                 = Registry.create();
            auto& panelLayout     = Registry.emplace<ECS::UILayoutComponent>( Panel ).Data;
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
        Draw( ctx, n.Registry, n.Canvas, first, &in );

        Probe           out;
        R2D::DrawList2D second;
        Draw( ctx, n.Registry, n.Canvas, second, &in );
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

// =========================================================================================================
// Ю1 — THE KEYBOARD IS THE SAME HIT TEST. У4 above asserts all four values through the POINTER; every one of
// those tests stays green on a build where Tab and Enter ignore the axis entirely, which is what `dev`
// shipped. So the claim here is not "the keyboard obeys Blocking" — it is that the two input paths reach the
// SAME SET, asserted as an equality across all four values, plus one pinned row so that "neither path works"
// cannot satisfy it (a count with no named row is satisfiable by breaking both sides).
// =========================================================================================================

namespace
{
    constexpr const char* kFired = "u1-fired";

    // Make the button report its own activation, whichever path fires it. Without an action a click writes
    // nothing to outClicked and both probes below would read "did not fire" forever.
    void ArmButton( Nested& n )
    {
        auto& b          = n.Registry.get<ECS::UIButtonComponent>( n.Button ).Data;
        b.Action         = ECS::UIButtonAction::SendMessage;
        b.OnClickMessage = kFired;
    }

    // Did the POINTER manage to fire the button, with the panel set to @p hit? Frame one elects (the hot
    // element is resolved a frame late by design), frame two releases over it.
    bool PointerFires( ECS::UIHitTest hit )
    {
        Nested n;
        n.SetHitTest( n.Panel, hit );
        ArmButton( n );

        UICanvasContext ctx;
        R2D::DrawList2D a, b;
        const UIInput   hold = At( 10.0f, 10.0f );
        Draw( ctx, n.Registry, n.Canvas, a, &hold );

        UIInput release       = At( 10.0f, 10.0f, /*down=*/false );
        release.MouseReleased = true;
        std::string clicked;
        Draw( ctx, n.Registry, n.Canvas, b, &release, &clicked );
        return clicked == kFired;
    }

    // Did the KEYBOARD? Frame one presses Tab, which fills the focus list and moves focus into it; frame two
    // presses Enter. The pointer is parked at (900,900) — over the panel, never over the button — and never
    // released, so nothing here can fire through the pointer path by accident.
    bool KeyboardFires( ECS::UIHitTest hit )
    {
        Nested n;
        n.SetHitTest( n.Panel, hit );
        ArmButton( n );

        UICanvasContext ctx;
        R2D::DrawList2D a, b;
        entt::entity    focused = entt::null;

        UIInput tab = At( 900.0f, 900.0f, /*down=*/false );
        tab.Tab     = true;
        Draw( ctx, n.Registry, n.Canvas, a, &tab, nullptr, &focused );

        UIInput enter = At( 900.0f, 900.0f, /*down=*/false );
        enter.Submit  = true;
        std::string clicked;
        Draw( ctx, n.Registry, n.Canvas, b, &enter, &clicked, &focused );
        return clicked == kFired;
    }

    // Where Tab PARKED the focus, with the panel set to @p hit. Separate from KeyboardFires because the two
    // gates are separate: Enter being inert on an unreachable control and Tab refusing to stop on it are
    // different properties, and a build with only the first still makes the user press Tab twice to get past
    // a control they cannot use. Measured: gating Enter alone leaves every assertion in (17) green.
    entt::entity FocusAfterTab( Nested& n, ECS::UIHitTest hit )
    {
        n.SetHitTest( n.Panel, hit );

        UICanvasContext ctx;
        R2D::DrawList2D dl;
        entt::entity    focused = entt::null;
        UIInput         tab     = At( 900.0f, 900.0f, /*down=*/false );
        tab.Tab                 = true;
        Draw( ctx, n.Registry, n.Canvas, dl, &tab, nullptr, &focused );
        return focused;
    }
} // namespace

// --- (17) The relation, over all four values ------------------------------------------------------------
TEST( UICanvasHitTest, TheKeyboardReachesExactlyWhatThePointerReaches )
{
    for ( const ECS::UIHitTest hit :
          { ECS::UIHitTest::All, ECS::UIHitTest::ChildrenOnly, ECS::UIHitTest::Blocking, ECS::UIHitTest::None } )
    {
        const bool pointer  = PointerFires( hit );
        const bool keyboard = KeyboardFires( hit );
        EXPECT_EQ( pointer, keyboard )
             << "with the ancestor's Hit Test = " << static_cast<int>( hit ) << " the pointer "
             << ( pointer ? "could" : "could not" ) << " fire the button and the keyboard "
             << ( keyboard ? "could" : "could not" )
             << " — the two paths must agree, and the greyed-out "
                "modal is exactly the case where a keyboard that disagrees is the whole defect";
    }

    // THE PINNED ROWS. An equality is satisfied just as well by both paths being dead, so say which way
    // round each end is. All must fire through both doors; Blocking must fire through neither.
    EXPECT_TRUE( PointerFires( ECS::UIHitTest::All ) ) << "the pointer stopped working entirely";
    EXPECT_TRUE( KeyboardFires( ECS::UIHitTest::All ) ) << "Tab+Enter no longer reaches a plain button";
    EXPECT_FALSE( KeyboardFires( ECS::UIHitTest::Blocking ) )
         << "Tab walked into a Blocking panel and Enter fired the button inside it";
}

// --- (17b) And Tab does not even STOP on a control the pointer cannot reach ------------------------------
//
// The focus LIST is gated as well as the activation, and this is the assertion that says so: with Enter
// alone gated, a Blocking panel still swallows a Tab stop — focus lands on something inert and the author
// has to press Tab twice to get anywhere, with nothing on screen explaining it. Two gates, two properties.
TEST( UICanvasHitTest, TabDoesNotStopOnAControlThePointerCannotReach )
{
    Nested all, blocking, none;
    EXPECT_EQ( FocusAfterTab( all, ECS::UIHitTest::All ), all.Button ) << "Tab no longer reaches a plain button";
    EXPECT_TRUE( FocusAfterTab( blocking, ECS::UIHitTest::Blocking ) == entt::null )
         << "Tab parked focus inside a Blocking panel";
    EXPECT_TRUE( FocusAfterTab( none, ECS::UIHitTest::None ) == entt::null )
         << "Tab parked focus inside a HitTest::None sub-tree";
}

// --- (17c) A focus the host already held does not fire Enter either -------------------------------------
//
// The focus list gate (17b) only decides where Tab can GO. `focused` belongs to the host and survives
// frames, so the case it cannot cover is a control that held focus legitimately and had an ancestor turned
// Blocking under it afterwards — a modal opening over a form is exactly that, and it has no pointer
// equivalent to compare against. Hence the second gate, on the focus test itself, and hence this test:
// removing it leaves (17) and (17b) entirely green.
TEST( UICanvasHitTest, EnterOnAFocusHeldFromBeforeDoesNotFireAnUnreachableButton )
{
    auto fires = []( ECS::UIHitTest hit )
    {
        Nested n;
        n.SetHitTest( n.Panel, hit );
        ArmButton( n );

        entt::entity    focused = n.Button; // handed, not tabbed: the panel changed under a live focus
        UICanvasContext ctx;
        R2D::DrawList2D dl;
        std::string     clicked;
        UIInput         enter = At( 900.0f, 900.0f, /*down=*/false );
        enter.Submit          = true;
        Draw( ctx, n.Registry, n.Canvas, dl, &enter, &clicked, &focused );
        return clicked == kFired;
    };

    EXPECT_TRUE( fires( ECS::UIHitTest::All ) ) << "Enter stopped working on a reachable focused button";
    EXPECT_FALSE( fires( ECS::UIHitTest::Blocking ) )
         << "Enter fired a button inside a Blocking panel because focus predated the panel's change";
    EXPECT_FALSE( fires( ECS::UIHitTest::None ) ) << "Enter fired a button inside a HitTest::None sub-tree";
}

// --- (18) The fourth keyboard door: typing ---------------------------------------------------------------
//
// Enter is not the only key that reaches a control. A focused UIInputField consumes TypedText and Backspace,
// and `focused` is the HOST's — it survives frames — so the case that has no pointer analogue at all is a
// field that held focus legitimately and then had an ancestor turned Blocking under it. Nothing in the
// pointer path can express that, which is why it is a test of its own rather than a row in (17).
TEST( UICanvasHitTest, AFieldOutOfTheHitTestsReachStopsAcceptingTypedText )
{
    auto typeInto = [&]( ECS::UIHitTest hit ) -> std::string
    {
        Nested n;
        n.SetHitTest( n.Panel, hit );

        // A field beside the button, inside the same panel.
        const entt::entity field = n.Registry.create();
        auto&              L     = n.Registry.emplace<ECS::UILayoutComponent>( field ).Data;
        L.AnchorMin              = { 0.0f, 0.0f };
        L.AnchorMax              = { 0.0f, 0.0f };
        L.OffsetMin              = { 0.0f, 100.0f };
        L.OffsetMax              = { 200.0f, 140.0f };
        n.Registry.emplace<ECS::UIInputFieldComponent>( field );
        n.Registry.emplace<ECS::RelationshipComponent>( field ).Parent = n.Panel;
        n.Registry.get<ECS::RelationshipComponent>( n.Panel ).Children.push_back( field );

        // Focus is HANDED to the field rather than tabbed to, which is the stale-focus case: it is what a
        // host holds after the field was legitimately focused and the panel changed afterwards.
        entt::entity    focused = field;
        UICanvasContext ctx;
        R2D::DrawList2D dl;
        UIInput         keys = At( 900.0f, 900.0f, /*down=*/false );
        keys.TypedText       = "x";
        Draw( ctx, n.Registry, n.Canvas, dl, &keys, nullptr, &focused );
        return n.Registry.get<ECS::UIInputFieldComponent>( field ).Data.Text;
    };

    EXPECT_EQ( typeInto( ECS::UIHitTest::All ), "x" ) << "a reachable field stopped accepting text";
    EXPECT_EQ( typeInto( ECS::UIHitTest::Blocking ), "" )
         << "a field inside a Blocking panel took keystrokes the pointer could never have delivered to it";
    EXPECT_EQ( typeInto( ECS::UIHitTest::None ), "" ) << "a field inside a HitTest::None sub-tree took keystrokes";
}

// =========================================================================================================
// Ю1 — THE CANVAS IS ASKED. RenderCanvas2D and the layout queries used to elect
// `*reg.view<UICanvasComponent>().begin()`, so a scene's second canvas was drawn by nothing, picked by
// nothing and measured by nothing, silently. These assert the relation "what you ask for is what you get",
// which is the only claim that a build electing the first canvas cannot satisfy.
// =========================================================================================================

namespace
{
    // Two canvases in one registry, each with a full-canvas panel of its own colour, so which canvas was
    // drawn is a question the vertex buffer answers.
    struct TwoCanvases
    {
        entt::registry Registry;
        entt::entity   CanvasA = entt::null, CanvasB = entt::null;
        entt::entity   PanelA = entt::null, PanelB = entt::null;

        static glm::vec3 ColorA()
        {
            return { 1.0f, 0.0f, 0.0f };
        }
        static glm::vec3 ColorB()
        {
            return { 0.0f, 1.0f, 0.0f };
        }

        TwoCanvases()
        {
            CanvasA = Make( ColorA(), PanelA );
            CanvasB = Make( ColorB(), PanelB );
        }

    private:
        entt::entity Make( const glm::vec3& rgb, entt::entity& panelOut )
        {
            const entt::entity canvas = Registry.create();
            auto&              cd     = Registry.emplace<ECS::UICanvasComponent>( canvas ).Data;
            cd.ScaleMode              = ECS::UICanvasScaleMode::Stretch;
            cd.ReferenceWidth         = kSide;
            cd.ReferenceHeight        = kSide;

            panelOut         = Registry.create();
            auto& layout     = Registry.emplace<ECS::UILayoutComponent>( panelOut ).Data;
            layout.AnchorMin = { 0.0f, 0.0f };
            layout.AnchorMax = { 1.0f, 1.0f };
            layout.OffsetMin = { 0.0f, 0.0f };
            layout.OffsetMax = { 0.0f, 0.0f };

            auto& p        = Registry.emplace<ECS::UIPanelComponent>( panelOut ).Data;
            p.Color        = rgb;
            p.Opacity      = 1.0f;
            p.CornerRadius = 0.0f;

            Registry.emplace<ECS::RelationshipComponent>( canvas ).Children.push_back( panelOut );
            Registry.emplace<ECS::RelationshipComponent>( panelOut ).Parent = canvas;
            return canvas;
        }
    };
} // namespace

// --- (19) Whichever canvas is named is the one that draws ------------------------------------------------
TEST( UICanvasSelection, TheCanvasThatWasAskedForIsTheOneDrawn )
{
    TwoCanvases t;

    UICanvasContext ctxA, ctxB;
    R2D::DrawList2D a, b;
    EXPECT_TRUE( Draw( ctxA, t.Registry, t.CanvasA, a ) );
    EXPECT_TRUE( Draw( ctxB, t.Registry, t.CanvasB, b ) );

    EXPECT_TRUE( RectOfColor( a, TwoCanvases::ColorA() ).has_value() );
    EXPECT_FALSE( RectOfColor( a, TwoCanvases::ColorB() ).has_value() )
         << "asking for canvas A drew canvas B's content too";

    // THE HALF THAT WAS BROKEN. On the electing build this one is empty: B is not `*view.begin()`, so
    // whatever the caller asked for, A came back.
    EXPECT_TRUE( RectOfColor( b, TwoCanvases::ColorB() ).has_value() )
         << "the second canvas was asked for and something else was drawn — this is the whole defect";
    EXPECT_FALSE( RectOfColor( b, TwoCanvases::ColorA() ).has_value() );
}

// --- (20) The layout queries answer about the SAME canvas the draw did ----------------------------------
//
// Pick and draw disagreeing is this project's recurring shape, and a second canvas is a fresh way to get it:
// the editor's pick used to walk canvas A whatever was on screen, so an element of canvas B was drawn where
// nothing could select it.
TEST( UICanvasSelection, ThePickAndTheScaleAnswerAboutTheCanvasTheyWereGiven )
{
    TwoCanvases t;

    EXPECT_EQ( Desert::UI::PickElement( t.Registry, t.CanvasB, { 500.0f, 500.0f }, kViewport ), t.PanelB );
    EXPECT_EQ( Desert::UI::PickElement( t.Registry, t.CanvasA, { 500.0f, 500.0f }, kViewport ), t.PanelA );

    Rect r;
    EXPECT_TRUE( Desert::UI::GetElementRect( t.Registry, t.CanvasB, t.PanelB, kViewport, r ) );
    EXPECT_FALSE( Desert::UI::GetElementRect( t.Registry, t.CanvasA, t.PanelB, kViewport, r ) )
         << "canvas A reported a rect for an element that is not in it";

    const auto scale = Desert::UI::CanvasScale( t.Registry, t.CanvasB, kViewport );
    ASSERT_TRUE( scale.IsSuccess() ) << scale.GetError();
    EXPECT_FLOAT_EQ( scale.GetValue(), 1.0f ); // Stretch
}

// --- (21) Not naming one is a refusal, never a default --------------------------------------------------
//
// The contract's §1.4 case: "there are two and I drew one of them" is a successful-looking answer to a
// question nobody could have asked. Both the renderer and the resolver have to say so out loud.
TEST( UICanvasSelection, NotNamingACanvasIsARefusalAndNotTheFirstOne )
{
    TwoCanvases t;

    UICanvasContext ctx;
    R2D::DrawList2D dl;
    const auto      unnamed = Desert::UI::RenderCanvas2D( ctx, t.Registry, entt::null, dl, kViewport );
    EXPECT_FALSE( unnamed.IsSuccess() ) << "RenderCanvas2D accepted no canvas and drew something anyway";
    EXPECT_TRUE( dl.GetVertices().empty() ) << "a refused walk still emitted geometry";

    // An entity that exists but is not a canvas is a DIFFERENT refusal — a caller bug, not an empty scene.
    R2D::DrawList2D dl2;
    const auto      notACanvas = Desert::UI::RenderCanvas2D( ctx, t.Registry, t.PanelA, dl2, kViewport );
    EXPECT_FALSE( notACanvas.IsSuccess() );
    EXPECT_NE( notACanvas.GetError(), unnamed.GetError() )
         << "'you named nothing' and 'you named a panel' came back as the same sentence";

    // And the resolver refuses to break the tie rather than handing back the first.
    EXPECT_EQ( Desert::UI::CanvasCount( t.Registry ), 2u );
    EXPECT_FALSE( Desert::UI::SoleCanvas( t.Registry ).IsSuccess() )
         << "SoleCanvas elected a winner out of two canvases — the exact behaviour this task removed";

    entt::registry empty;
    EXPECT_EQ( Desert::UI::CanvasCount( empty ), 0u );
    EXPECT_FALSE( Desert::UI::SoleCanvas( empty ).IsSuccess() );
}

// --- (22) CanvasOf derives the answer instead of guessing it --------------------------------------------
//
// This is what the editor asks: an element already names its canvas by being inside it. It is exact, and it
// is the reason the viewport no longer needs an election at all when something is selected.
TEST( UICanvasSelection, CanvasOfWalksUpToTheCanvasTheElementIsActuallyIn )
{
    TwoCanvases t;

    EXPECT_EQ( Desert::UI::CanvasOf( t.Registry, t.PanelB ), t.CanvasB );
    EXPECT_EQ( Desert::UI::CanvasOf( t.Registry, t.PanelA ), t.CanvasA );
    EXPECT_EQ( Desert::UI::CanvasOf( t.Registry, t.CanvasB ), t.CanvasB ) << "a canvas is its own canvas";

    const entt::entity orphan = t.Registry.create();
    EXPECT_TRUE( Desert::UI::CanvasOf( t.Registry, orphan ) == entt::null );
    EXPECT_TRUE( Desert::UI::CanvasOf( t.Registry, entt::null ) == entt::null );

    // A parent cycle is authorable (the hierarchy panel reparents), and this must return rather than hang.
    const entt::entity a = t.Registry.create(), b = t.Registry.create();
    t.Registry.emplace<ECS::RelationshipComponent>( a ).Parent = b;
    t.Registry.emplace<ECS::RelationshipComponent>( b ).Parent = a;
    EXPECT_TRUE( Desert::UI::CanvasOf( t.Registry, a ) == entt::null );
}

// =========================================================================================================
// Ю8 — THE RENDER TRANSFORM, AND THE ONE THING THAT HAD TO BE TESTED ABOUT IT
//
// A rotated element has two halves that can each be right on their own: the quad that reaches the screen,
// and the region the pointer is accepted in. Testing them separately is exactly the mistake this project
// keeps paying for — so what is asserted below is their AGREEMENT, and it is asserted against the geometry
// the walk actually emitted rather than against a rect recomputed by the test.
//
// The drawn quad is read out of the draw list. The elected region is read out of the context. For a grid of
// sample points the two must give the same answer at every point; a rotation applied to one and not the
// other reddens this at roughly a quarter of the samples, and applying it in the WRONG DIRECTION to the
// pointer reddens it too (an inverse-vs-forward slip is the likely defect here, and it is symmetric about
// the pivot, so a centre-pivot test alone would pass — which is why the pivot below is a corner).
// =========================================================================================================

namespace
{
    // A canvas with ONE panel, sharp-cornered so it emits exactly one quad and the first four vertices of
    // the list ARE its screen corners. Rotation / Scale / Pivot are the test's to set.
    struct XformFixture
    {
        entt::registry Registry;
        entt::entity   Canvas = entt::null;
        entt::entity   Panel  = entt::null;

        XformFixture()
        {
            Canvas                 = Registry.create();
            auto& canvas           = Registry.emplace<ECS::UICanvasComponent>( Canvas ).Data;
            canvas.ScaleMode       = ECS::UICanvasScaleMode::Stretch;
            canvas.ReferenceWidth  = kSide;
            canvas.ReferenceHeight = kSide;

            Panel            = Registry.create();
            auto& layout     = Registry.emplace<ECS::UILayoutComponent>( Panel ).Data;
            layout.AnchorMin = { 0.0f, 0.0f };
            layout.AnchorMax = { 0.0f, 0.0f };
            layout.OffsetMin = { 200.0f, 300.0f };
            layout.OffsetMax = { 500.0f, 420.0f };

            auto& panel        = Registry.emplace<ECS::UIPanelComponent>( Panel ).Data;
            panel.CornerRadius = 0.0f; // sharp => AddRectFilled takes the four-vertex path
            panel.Opacity      = 1.0f;

            Registry.emplace<ECS::RelationshipComponent>( Canvas ).Children.push_back( Panel );
            Registry.emplace<ECS::RelationshipComponent>( Panel ).Parent = Canvas;
        }

        ECS::UILayoutData& Layout( entt::entity e )
        {
            return Registry.get<ECS::UILayoutComponent>( e ).Data;
        }
    };

    // Where @p p sits relative to the convex quad @p q (given in order). All four cross products share a
    // sign for a point inside, whichever way round the quad is wound — which matters because a negative
    // scale flips the winding.
    //
    // ON_EDGE IS A THIRD ANSWER AND IT IS NOT A HEDGE. A sample within half a pixel of a rotated edge is
    // a tie the two sides settle differently for reasons that are not this test's subject: the pointer
    // test is closed on both bounds (`>=` and `<=`) while the sign of a cross product a few ulps from
    // zero is whatever the rotation's rounding made it. The pivot is itself a CORNER of the quad, so
    // there is always at least one such sample. Ties are skipped and counted; the assertion is about
    // every point that is unambiguously in or out.
    enum class Where
    {
        Inside,
        Outside,
        OnEdge
    };

    Where WhereInQuad( const std::array<glm::vec2, 4>& q, const glm::vec2& p )
    {
        int   positive = 0, negative = 0;
        float nearest = 1e30f;
        for ( int i = 0; i < 4; ++i )
        {
            const glm::vec2 a   = q[i];
            const glm::vec2 b   = q[( i + 1 ) % 4];
            const float     c   = ( b.x - a.x ) * ( p.y - a.y ) - ( b.y - a.y ) * ( p.x - a.x );
            const float     len = glm::length( b - a );
            if ( len > 0.0f )
                nearest = std::min( nearest, std::fabs( c ) / len ); // px from the edge's line
            if ( c > 0.0f )
                ++positive;
            if ( c < 0.0f )
                ++negative;
        }
        if ( nearest < 0.5f )
            return Where::OnEdge;
        return ( positive == 0 || negative == 0 ) ? Where::Inside : Where::Outside;
    }

    // The first four vertices of the list, i.e. the panel's quad where it landed on screen.
    std::array<glm::vec2, 4> DrawnQuad( const R2D::DrawList2D& dl )
    {
        std::array<glm::vec2, 4> q{};
        EXPECT_GE( dl.GetVertices().size(), 4u ) << "the panel emitted no quad at all";
        for ( std::size_t i = 0; i < 4 && i < dl.GetVertices().size(); ++i )
            q[i] = dl.GetVertices()[i].Position;
        return q;
    }

    // Walk once with the pointer at @p p and answer whether the walk elected @p e. One frame is enough:
    // the election is finished by the time RenderCanvas2D returns (ctx.Hot = ctx.HotNext).
    bool ElectsAt( XformFixture& f, entt::entity e, const glm::vec2& p )
    {
        UICanvasContext ctx;
        R2D::DrawList2D dl;
        const UIInput   in = At( p.x, p.y, /*down=*/false );
        Draw( ctx, f.Registry, f.Canvas, dl, &in );
        return ctx.Hot == e;
    }
} // namespace

// The relation, over a grid dense enough to straddle every edge of a turned rectangle.
TEST( UICanvasContext, WhereARotatedElementIsDrawnIsWhereItTakesThePointer )
{
    XformFixture f;
    f.Layout( f.Panel ).Rotation = 30.0f;
    f.Layout( f.Panel ).Pivot    = { 0.0f, 0.0f }; // a CORNER: an inverse/forward slip is not symmetric here

    R2D::DrawList2D dl;
    UICanvasContext ctx;
    Draw( ctx, f.Registry, f.Canvas, dl, nullptr );
    const std::array<glm::vec2, 4> quad = DrawnQuad( dl );

    // The quad really did move — otherwise this test would be asserting agreement about nothing.
    ASSERT_GT( std::fabs( quad[0].y - quad[1].y ), 1.0f ) << "the panel was drawn axis-aligned";

    int inside = 0, disagreements = 0, ties = 0;
    for ( float y = 20.0f; y < 900.0f; y += 20.0f )
        for ( float x = 20.0f; x < 900.0f; x += 20.0f )
        {
            const glm::vec2 p     = { x, y };
            const Where     where = WhereInQuad( quad, p );
            if ( where == Where::OnEdge )
            {
                ++ties;
                continue;
            }
            const bool drawn   = where == Where::Inside;
            const bool elected = ElectsAt( f, f.Panel, p );
            inside += drawn ? 1 : 0;
            if ( drawn != elected )
            {
                ++disagreements;
                if ( disagreements <= 5 )
                    ADD_FAILURE() << "at (" << x << "," << y << ") the element is "
                                  << ( drawn ? "DRAWN but not electable" : "electable but NOT DRAWN" );
            }
        }

    EXPECT_EQ( disagreements, 0 );
    EXPECT_GT( inside, 40 ) << "the sample grid never landed on the element, so it proved nothing";
    // The half-pixel tolerance must stay a minority report, or it would be the tolerance being measured
    // rather than the agreement. Expressed against the element's own sample count rather than as a
    // number, because that is the quantity it has to be small compared to.
    EXPECT_LT( ties * 4, inside ) << ties << " of the samples were within half a pixel of an edge, against "
                                  << inside << " unambiguously inside";
}

// Propagation, and it is the relation again one level down: the child states no transform of its own, so
// everything about where it is drawn AND where it is clickable comes from its parent.
TEST( UICanvasContext, AChildOfARotatedParentIsDrawnAndPickedWhereTheParentCarriedIt )
{
    XformFixture       f;
    const entt::entity child = f.Registry.create();
    auto&              cl    = f.Registry.emplace<ECS::UILayoutComponent>( child ).Data;
    cl.AnchorMin             = { 0.0f, 0.0f };
    cl.AnchorMax             = { 0.0f, 0.0f };
    cl.OffsetMin             = { 20.0f, 20.0f };
    cl.OffsetMax             = { 120.0f, 70.0f };
    auto& cp                 = f.Registry.emplace<ECS::UIPanelComponent>( child ).Data;
    cp.CornerRadius          = 0.0f;
    f.Registry.emplace<ECS::RelationshipComponent>( child ).Parent = f.Panel;
    f.Registry.get<ECS::RelationshipComponent>( f.Panel ).Children.push_back( child );

    // Where is the child's centre with the parent straight? Read it from the drawing, not computed here.
    glm::vec2 straightCentre;
    {
        R2D::DrawList2D dl;
        UICanvasContext ctx;
        Draw( ctx, f.Registry, f.Canvas, dl, nullptr );
        ASSERT_GE( dl.GetVertices().size(), 8u ); // parent quad, then the child's
        straightCentre = ( dl.GetVertices()[4].Position + dl.GetVertices()[6].Position ) * 0.5f;
    }

    f.Layout( f.Panel ).Rotation = 90.0f; // the PARENT turns; the child says nothing about transforms
    glm::vec2                turnedCentre;
    std::array<glm::vec2, 4> childQuad{};
    {
        R2D::DrawList2D dl;
        UICanvasContext ctx;
        Draw( ctx, f.Registry, f.Canvas, dl, nullptr );
        ASSERT_GE( dl.GetVertices().size(), 8u );
        for ( int i = 0; i < 4; ++i )
            childQuad[i] = dl.GetVertices()[4 + i].Position;
        turnedCentre = ( childQuad[0] + childQuad[2] ) * 0.5f;
    }

    // A quarter turn about the parent's centre (350,360) sends the child's centre from (270,345) to
    // (365,280) — written out rather than derived, so an error in the composition cannot cancel itself.
    EXPECT_NEAR( straightCentre.x, 270.0f, 1e-2f );
    EXPECT_NEAR( straightCentre.y, 345.0f, 1e-2f );
    EXPECT_NEAR( turnedCentre.x, 365.0f, 1e-2f );
    EXPECT_NEAR( turnedCentre.y, 280.0f, 1e-2f );

    // And the pointer followed it: the child is electable where it now is and not where it used to be.
    EXPECT_TRUE( ElectsAt( f, child, turnedCentre ) )
         << "the child was drawn at its parent's rotation but does not take the pointer there";
    EXPECT_FALSE( ElectsAt( f, child, straightCentre ) )
         << "the child still takes the pointer at the place it was drawn BEFORE the parent turned";

    // The child's own quad is a 100x50 rectangle whichever way it is turned — a parent transform must
    // carry a child, not restretch it.
    EXPECT_NEAR( glm::length( childQuad[1] - childQuad[0] ), 100.0f, 1e-2f );
    EXPECT_NEAR( glm::length( childQuad[3] - childQuad[0] ), 50.0f, 1e-2f );
}

// PICKELEMENT IS A SECOND IMPLEMENTATION OF THE SAME QUESTION (the editor's WYSIWYG select), and the
// header of UICanvasLayout.hpp says in as many words that the two must not drift. Under a transform they
// have a new way to drift, so the agreement is pinned here too.
TEST( UICanvasContext, TheEditorsPickAgreesWithTheWalkAboutARotatedElement )
{
    XformFixture f;
    f.Layout( f.Panel ).Rotation = -40.0f;
    f.Layout( f.Panel ).Scale    = { 1.3f, 0.7f };
    f.Layout( f.Panel ).Pivot    = { 1.0f, 0.0f };

    int disagreements = 0, hits = 0;
    for ( float y = 20.0f; y < 900.0f; y += 25.0f )
        for ( float x = 20.0f; x < 900.0f; x += 25.0f )
        {
            const bool picked  = Desert::UI::PickElement( f.Registry, f.Canvas, { x, y }, kViewport ) == f.Panel;
            const bool elected = ElectsAt( f, f.Panel, { x, y } );
            hits += picked ? 1 : 0;
            if ( picked != elected )
            {
                ++disagreements;
                if ( disagreements <= 5 )
                    ADD_FAILURE() << "at (" << x << "," << y << ") the editor pick says " << picked
                                  << " and the renderer's election says " << elected;
            }
        }
    EXPECT_EQ( disagreements, 0 );
    EXPECT_GT( hits, 40 ) << "the grid never hit the element, so the agreement was vacuous";
}

// PIVOT WITH ITS CONSUMER. Д26 deleted this field because nothing read it; the assertion that it is not
// dead again is that the SAME rotation about two different pivots puts the element in two different
// places — and that both places are hit-testable, so it moved the pointer with the picture.
TEST( UICanvasContext, TheSameRotationAboutTwoPivotsLandsInTwoPlaces )
{
    const auto centreOfPanelWith = []( const glm::vec2& pivot )
    {
        XformFixture f;
        f.Layout( f.Panel ).Rotation = 45.0f;
        f.Layout( f.Panel ).Pivot    = pivot;
        R2D::DrawList2D dl;
        UICanvasContext ctx;
        Draw( ctx, f.Registry, f.Canvas, dl, nullptr );
        EXPECT_GE( dl.GetVertices().size(), 4u );
        return ( dl.GetVertices()[0].Position + dl.GetVertices()[2].Position ) * 0.5f;
    };

    const glm::vec2 aboutCentre = centreOfPanelWith( { 0.5f, 0.5f } );
    const glm::vec2 aboutCorner = centreOfPanelWith( { 0.0f, 0.0f } );

    // About its own centre the element does not move at all; about its top-left corner it swings away.
    EXPECT_NEAR( aboutCentre.x, 350.0f, 1e-2f );
    EXPECT_NEAR( aboutCentre.y, 360.0f, 1e-2f );
    EXPECT_GT( glm::length( aboutCorner - aboutCentre ), 50.0f )
         << "Pivot did not move the picture, which is what got the field deleted in the first place";

    // ...and the pointer went with it, at both pivots.
    {
        XformFixture f;
        f.Layout( f.Panel ).Rotation = 45.0f;
        f.Layout( f.Panel ).Pivot    = { 0.0f, 0.0f };
        EXPECT_TRUE( ElectsAt( f, f.Panel, aboutCorner ) );
        EXPECT_FALSE( ElectsAt( f, f.Panel, aboutCentre ) )
             << "the corner-pivot element still takes the pointer where a centre-pivot one would be";
    }
}

// A clip is a scissor and a scissor is a box, so a rotated clipper clips to the box around itself. That is
// a deliberate limit (clipping to the quadrilateral needs a stencil) and it is pinned here so it is a
// DECISION rather than something nobody noticed: what must hold is that the pointer is refused in exactly
// the region the pixels were, which is that same box and not the unrotated rect.
TEST( UICanvasContext, ARotatedClipperClipsThePointerToTheSameBoxItClippedThePixels )
{
    XformFixture f;
    f.Layout( f.Panel ).ClipContents = true;
    f.Layout( f.Panel ).Rotation     = 45.0f;

    const entt::entity child = f.Registry.create();
    auto&              cl    = f.Registry.emplace<ECS::UILayoutComponent>( child ).Data;
    cl.AnchorMin             = { 0.0f, 0.0f };
    cl.AnchorMax             = { 0.0f, 0.0f };
    cl.OffsetMin             = { 0.0f, 0.0f };
    cl.OffsetMax             = { 300.0f, 120.0f };
    f.Registry.emplace<ECS::UIPanelComponent>( child ).Data.CornerRadius = 0.0f;
    f.Registry.emplace<ECS::RelationshipComponent>( child ).Parent       = f.Panel;
    f.Registry.get<ECS::RelationshipComponent>( f.Panel ).Children.push_back( child );

    R2D::DrawList2D dl;
    UICanvasContext ctx;
    Draw( ctx, f.Registry, f.Canvas, dl, nullptr );

    // The child's command carries the scissor the pixels were cut with.
    glm::vec4 clip{ 0.0f };
    for ( const auto& cmd : dl.GetCommands() )
        if ( cmd.ClipRect.z > 0.0f )
            clip = cmd.ClipRect;
    ASSERT_GT( clip.z, 0.0f ) << "nothing was clipped at all";

    // A point inside that box and inside the child's own rect is electable; one outside the box is not,
    // and the two together are what makes this an assertion about the SAME region twice.
    const glm::vec2 inBox( clip.x + clip.z * 0.5f, clip.y + clip.w * 0.5f );
    EXPECT_TRUE( ElectsAt( f, child, inBox ) );
    EXPECT_FALSE( ElectsAt( f, child, { clip.x - 5.0f, clip.y - 5.0f } ) )
         << "the pointer was accepted outside the box the scissor cut";
}

// TWO LEVELS, BOTH TURNED — and this one exists because the single-level test above did NOT catch the
// mutation it should have. Making DrawList2D::PushTransform REPLACE the current matrix instead of
// composing with it left that test green, because its child stated no transform of its own and a stack
// one deep cannot tell replacement from composition. The mutation was EQUIVALENT there, not survived;
// what it needed was a case where the stack is two deep, which is this one.
TEST( UICanvasContext, TwoTurnedLevelsComposeRatherThanReplace )
{
    XformFixture f;
    f.Layout( f.Panel ).Rotation = 30.0f;
    f.Layout( f.Panel ).Pivot    = { 0.5f, 0.5f };

    const entt::entity child = f.Registry.create();
    auto&              cl    = f.Registry.emplace<ECS::UILayoutComponent>( child ).Data;
    cl.AnchorMin             = { 0.0f, 0.0f };
    cl.AnchorMax             = { 0.0f, 0.0f };
    cl.OffsetMin             = { 20.0f, 20.0f };
    cl.OffsetMax             = { 120.0f, 70.0f };
    cl.Rotation              = 60.0f; // 30 + 60 = 90 composed, which is the one angle written down exactly
    cl.Pivot                 = { 0.5f, 0.5f };
    f.Registry.emplace<ECS::UIPanelComponent>( child ).Data.CornerRadius = 0.0f;
    f.Registry.emplace<ECS::RelationshipComponent>( child ).Parent       = f.Panel;
    f.Registry.get<ECS::RelationshipComponent>( f.Panel ).Children.push_back( child );

    R2D::DrawList2D dl;
    UICanvasContext ctx;
    Draw( ctx, f.Registry, f.Canvas, dl, nullptr );
    ASSERT_GE( dl.GetVertices().size(), 8u );

    std::array<glm::vec2, 4> quad{};
    for ( int i = 0; i < 4; ++i )
        quad[i] = dl.GetVertices()[4 + i].Position;

    // The child's own top edge is 100 px long and, at 90 degrees composed, must be VERTICAL. Replacing
    // instead of composing would leave it at the child's own 60 degrees, i.e. 50 px of run.
    const glm::vec2 topEdge = quad[1] - quad[0];
    EXPECT_NEAR( glm::length( topEdge ), 100.0f, 1e-2f );
    EXPECT_NEAR( topEdge.x, 0.0f, 1e-2f ) << "the child was drawn at its own rotation, not at the "
                                             "composition of its own with its parent's";
    EXPECT_NEAR( std::fabs( topEdge.y ), 100.0f, 1e-2f );

    // WHERE the child ends up, worked out by hand so the assertion is independent of the code under
    // test. The child's own 60 degrees is about its OWN centre, which that rotation leaves at (270,345);
    // the parent's 30 degrees is about (350,360), so the offset (-80,-15) becomes
    //   ( 0.866*-80 - 0.5*-15, 0.5*-80 + 0.866*-15 ) = (-61.782, -52.990)
    // and the centre lands at (288.218, 307.010). Composed the other way round it would not: the two
    // rotations are about different points, so the order shows in the position as well as the angle.
    const glm::vec2 centre = ( quad[0] + quad[2] ) * 0.5f;
    EXPECT_NEAR( centre.x, 288.218f, 1e-2f );
    EXPECT_NEAR( centre.y, 307.010f, 1e-2f );

    // The pointer is at the composition too, which is the half a draw-list test cannot reach.
    EXPECT_TRUE( ElectsAt( f, child, centre ) );
    EXPECT_EQ( Desert::UI::PickElement( f.Registry, f.Canvas, centre, kViewport ), child )
         << "the editor's pick disagrees with the walk about a doubly-rotated child";
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
