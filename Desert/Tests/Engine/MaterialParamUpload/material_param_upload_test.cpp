// A material parameter must reach EVERY (frame in flight x renderer slot) copy of its uniform buffer,
// by whichever route the material was submitted.
//
// The defect this suite exists for: a shader-graph material with a Color/Float Param rendered black on
// two frames out of three. The values were right; they had been written into exactly ONE of the three
// frame-in-flight copies, because UniformBufferProperty::UpdateFields writes the copy belonging to the
// pair that is recording and nobody came back on the following frames to write the others. Materials
// submitted as a MaterialComponent shader override escaped it only by accident — they re-apply their
// parameters every frame, and SetParamRaw calls UpdateFields as a side effect.
//
// So the relation asserted here is the one that was broken: THE TWO ROUTES MUST LEAVE THE SAME BYTES IN
// THE SAME COPIES. Route A restates its value every frame (the override producer). Route B applies it
// once and then flushes every frame (the material-asset producer, as MeshRenderer now drives it through
// DataDrivenMaterial::FlushParameterBuffers). Anything that makes B stop serving a copy that A serves is
// the defect coming back.
//
// The field writes below go through UniformBufferProperty::WriteField rather than reaching the
// FieldProperty directly, because that is now the only way in: a bare field write left the owning
// buffer unaware of which of its two fill routes had been used. See Tests/Engine/BufferFillKind.
//
// No GPU: UniformBufferProperty and FieldProperty are header-only, and ShaderResources::UniformBuffer is
// abstract, so the copies live in a std::vector here. The copy a write lands in is resolved with the
// PRODUCTION function, ShaderResources::BufferCopyIndex — the same one VulkanUniformBuffer uses. A test
// that recomputed that arithmetic itself would be the very thing this engine keeps getting burned by:
// two places obliged to agree, with nothing checking that they do.

#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/FrameManager.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Engine/ShaderResources/BufferCopyLayout.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <set>
#include <vector>

using namespace Desert;
using namespace Desert::ShaderResources;

namespace
{
    constexpr uint32_t kFramesInFlight = 3; // what the editor's swapchain actually runs
    constexpr uint32_t kSlots          = Engine::kMaxRendererSlots;
    constexpr uint32_t kFieldSize      = sizeof( float ) * 4;

    // Records what the GPU would have received. NOT a second implementation of the copy layout: it
    // resolves the target copy with the production BufferCopyIndex, so if that arithmetic ever changes
    // this instrument changes with it.
    class RecordingUniformBuffer final : public UniformBuffer
    {
    public:
        explicit RecordingUniformBuffer( const ShaderLayout::UniformBuffer& model )
             : UniformBuffer( model ), m_Copies( BufferCopyCount( kFramesInFlight, kSlots ),
                                                 std::vector<std::byte>( model.Size, std::byte{ 0 } ) )
        {
        }

        void SetData( const void* data, uint32_t size, uint32_t offset ) override
        {
            auto& copy = m_Copies[CurrentCopy()];
            ASSERT_LE( offset + size, copy.size() );
            std::memcpy( copy.data() + offset, data, size );
        }

        uint8_t* MapMemory() override
        {
            return reinterpret_cast<uint8_t*>( m_Copies[CurrentCopy()].data() );
        }
        void UnmapMemory() override
        {
        }
        const void* GetData() const override
        {
            return m_Copies[CurrentCopy()].data();
        }

        /// Bytes the (frame x slot) pair would read.
        const std::vector<std::byte>& Copy( uint32_t frame, uint32_t slot ) const
        {
            return m_Copies[BufferCopyIndex( frame, slot, kSlots )];
        }

        static uint32_t CurrentCopy()
        {
            return BufferCopyIndex( Engine::FrameManager::GetInstance().GetCurrentFrameIndex(),
                                    EngineContext::GetInstance().GetActiveRendererSlot(), kSlots );
        }

    private:
        std::vector<std::vector<std::byte>> m_Copies;
    };

    ShaderLayout::UniformBuffer MakeModel()
    {
        ShaderLayout::UniformBuffer model;
        model.Name         = "MaterialUB";
        model.BindingPoint = 1;
        model.Size         = kFieldSize;
        model.Fields.push_back( { Core::Formats::ShaderValueType::Float4, "Tint", kFieldSize, 0, 1 } );
        return model;
    }

    // The four bytes an artist authored.
    std::vector<std::byte> Authored( float r, float g, float b, float a )
    {
        const float            v[4] = { r, g, b, a };
        std::vector<std::byte> out( kFieldSize );
        std::memcpy( out.data(), v, kFieldSize );
        return out;
    }

    class MaterialParamUpload : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // Both are plain counter singletons; nothing here touches a device.
            EngineContext::CreateInstance();
            Engine::FrameManager::CreateInstance().Initialize( kFramesInFlight );
            EngineContext::GetInstance().SetActiveRendererSlot( 0 );
        }
    };

    // How the two producers differ, expressed once so the tests below read as the comparison they are.
    enum class Route
    {
        RestateEveryFrame, // MaterialComponent shader override: re-applies its params per draw
        ApplyOnceThenFlush // material asset: params applied when the asset loaded, flushed per draw
    };

    // Drives @p frameCount frames of one route and returns the buffer it wrote. Frame numbering starts
    // from whatever FrameManager is on, so callers comparing two routes re-Initialize between them.
    std::shared_ptr<RecordingUniformBuffer>
    RunRoute( Route route, uint32_t frameCount, const std::vector<std::byte>& value, bool flushPerDraw = true )
    {
        auto                           buffer = std::make_shared<RecordingUniformBuffer>( MakeModel() );
        Graphic::UniformBufferProperty prop( buffer );

        if ( route == Route::ApplyOnceThenFlush )
        {
            // Applied when the asset loads — one write, outside any recording.
            prop.WriteField( prop.GetField( "Tint" ), value.data(), value.size() );
            prop.UpdateFields();
        }

        for ( uint32_t i = 0; i < frameCount; ++i )
        {
            if ( route == Route::RestateEveryFrame )
            {
                prop.WriteField( prop.GetField( "Tint" ), value.data(), value.size() );
                prop.UpdateFields();
            }
            else if ( flushPerDraw && prop.HasDirtyFields() )
            {
                prop.UpdateFields();
            }
            Engine::FrameManager::GetInstance().NextFrame();
        }

        return buffer;
    }

    /// Copies a renderer on @p slot would read across every frame in flight.
    std::set<std::vector<std::byte>> CopiesSeenBy( const RecordingUniformBuffer& buffer, uint32_t slot )
    {
        std::set<std::vector<std::byte>> seen;
        for ( uint32_t f = 0; f < kFramesInFlight; ++f )
            seen.insert( buffer.Copy( f, slot ) );
        return seen;
    }
} // namespace

// The headline relation: one value, two routes, identical bytes in identical copies.
TEST_F( MaterialParamUpload, BothRoutesLeaveTheSameBytesInEveryFrameCopy )
{
    const auto value = Authored( 0.9f, 0.12f, 0.08f, 1.0f );

    auto viaOverride = RunRoute( Route::RestateEveryFrame, kFramesInFlight * 2, value );
    Engine::FrameManager::GetInstance().Initialize( kFramesInFlight );
    auto viaAsset = RunRoute( Route::ApplyOnceThenFlush, kFramesInFlight * 2, value );

    for ( uint32_t f = 0; f < kFramesInFlight; ++f )
    {
        EXPECT_EQ( viaAsset->Copy( f, 0 ), value ) << "asset route left frame copy " << f << " unwritten";
        EXPECT_EQ( viaAsset->Copy( f, 0 ), viaOverride->Copy( f, 0 ) )
             << "the two routes disagree on frame copy " << f;
    }
}

// The defect, stated as the thing that must not be true: no frame may read a copy nobody wrote.
// Unwritten copies are zero-filled by the allocator, which is exactly why the mesh rendered BLACK.
TEST_F( MaterialParamUpload, NoFrameReadsAnUnwrittenCopy )
{
    const auto value  = Authored( 0.08f, 0.25f, 0.95f, 1.0f );
    auto       buffer = RunRoute( Route::ApplyOnceThenFlush, kFramesInFlight * 2, value );

    const auto seen = CopiesSeenBy( *buffer, 0 );
    ASSERT_EQ( seen.size(), 1u ) << "the frames in flight do not agree — some copy was never served";
    EXPECT_EQ( *seen.begin(), value );
}

// SABOTAGE, kept as a test: applying the value once and never flushing again is precisely the code that
// shipped, and it must be distinguishable from the fix. If this ever starts finding every copy written,
// the mechanism has changed and the two tests above have stopped guarding anything.
TEST_F( MaterialParamUpload, ApplyingOnceWithoutAPerDrawFlushServesExactlyOneCopy )
{
    const auto value  = Authored( 0.9f, 0.12f, 0.08f, 1.0f );
    auto       buffer = RunRoute( Route::ApplyOnceThenFlush, kFramesInFlight * 2, value, /*flushPerDraw=*/false );

    uint32_t written = 0;
    for ( uint32_t f = 0; f < kFramesInFlight; ++f )
        if ( buffer->Copy( f, 0 ) == value )
            ++written;

    EXPECT_EQ( written, 1u ) << "one write outside recording must reach exactly one frame copy";
    EXPECT_LT( written, kFramesInFlight ) << "the remaining copies are the black frames";
}

// A view that opens later must not inherit the first view's drained dirty budget: its own copies are
// still owed the value. This is the multi-renderer half of the same relation, and it is what a live
// material-preview window will stand on.
//
// The middle assertion — slot 1 is still UNTOUCHED while only slot 0 has recorded — is the load-bearing
// one, and it was added after a sabotage went green: with the slot term removed from BufferCopyIndex the
// two slots share one copy, so checking only that both ended up holding the value passed happily on a
// layout with no slot dimension at all. Asserting that slot 0's writes did NOT reach slot 1 is what
// makes this test about slots rather than about bytes.
TEST_F( MaterialParamUpload, ASecondRendererSlotIsStillOwedTheValue )
{
    const auto                     value  = Authored( 0.1f, 0.85f, 0.15f, 1.0f );
    const auto                     zeros  = std::vector<std::byte>( kFieldSize, std::byte{ 0 } );
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( MakeModel() );
    Graphic::UniformBufferProperty prop( buffer );

    prop.WriteField( prop.GetField( "Tint" ), value.data(), value.size() );
    prop.UpdateFields();

    // Slot 0 records alone for a while — long enough to drain its own dirty window.
    EngineContext::GetInstance().SetActiveRendererSlot( 0 );
    for ( uint32_t i = 0; i < kFramesInFlight * 2; ++i )
    {
        if ( prop.HasDirtyFields() )
            prop.UpdateFields();
        Engine::FrameManager::GetInstance().NextFrame();
    }

    for ( uint32_t f = 0; f < kFramesInFlight; ++f )
    {
        EXPECT_EQ( buffer->Copy( f, 0 ), value ) << "slot 0 lost frame copy " << f;
        EXPECT_EQ( buffer->Copy( f, 1 ), zeros )
             << "slot 0's write reached slot 1's copy " << f
             << " — the slot dimension is not separating "
                "the two views, which is the defect Docs/RENDERER_FRAME_STATE.md exists for";
    }

    // Now a second view opens and records. It has served none of its own copies yet.
    EngineContext::GetInstance().SetActiveRendererSlot( 1 );
    for ( uint32_t i = 0; i < kFramesInFlight * 2; ++i )
    {
        if ( prop.HasDirtyFields() )
            prop.UpdateFields();
        Engine::FrameManager::GetInstance().NextFrame();
    }

    for ( uint32_t f = 0; f < kFramesInFlight; ++f )
    {
        EXPECT_EQ( buffer->Copy( f, 0 ), value ) << "slot 0 lost frame copy " << f;
        EXPECT_EQ( buffer->Copy( f, 1 ), value ) << "slot 1 never received frame copy " << f;
    }
}

// The layout relation the instrument above depends on, asserted rather than trusted: distinct pairs
// never collide, and every index is inside the allocation.
TEST( BufferCopyLayout, DistinctPairsNeverShareACopy )
{
    std::set<uint32_t> seen;
    for ( uint32_t f = 0; f < kFramesInFlight; ++f )
    {
        for ( uint32_t s = 0; s < kSlots; ++s )
        {
            const uint32_t idx = BufferCopyIndex( f, s, kSlots );
            EXPECT_LT( idx, BufferCopyCount( kFramesInFlight, kSlots ) );
            EXPECT_TRUE( seen.insert( idx ).second ) << "frame " << f << " slot " << s << " collides";
        }
    }
    EXPECT_EQ( seen.size(), BufferCopyCount( kFramesInFlight, kSlots ) );
}

// Out-of-range slots fold onto 0 — the same fallback SceneRenderer takes when it runs out of leases.
// In range this must be the identity, or a stray slot would quietly share another view's copy.
TEST( BufferCopyLayout, OutOfRangeSlotFoldsOntoZeroAndInRangeDoesNot )
{
    EXPECT_EQ( BufferCopyIndex( 2, kSlots, kSlots ), BufferCopyIndex( 2, 0, kSlots ) );
    EXPECT_EQ( BufferCopyIndex( 2, kSlots + 7, kSlots ), BufferCopyIndex( 2, 0, kSlots ) );
    for ( uint32_t s = 1; s < kSlots; ++s )
        EXPECT_NE( BufferCopyIndex( 2, s, kSlots ), BufferCopyIndex( 2, 0, kSlots ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
