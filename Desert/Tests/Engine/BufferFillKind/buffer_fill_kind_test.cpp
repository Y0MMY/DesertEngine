// A uniform buffer is filled ONE way, and the other way is refused by name rather than silently
// applied.
//
// The defect this suite exists for is recorded in Docs/RENDERER_FRAME_STATE.md's neighbourhood and in
// DataDrivenMaterial's comments: a fix for material flicker walked every uniform buffer of a material
// and flushed its fields. `CameraUB`, `TimeUB` and `DirectionLightsUB` are written WHOLE, by one memcpy
// of a C++ struct, and their per-field shadow copies (FieldProperty::m_LocalData) are allocated with a
// bare `new std::byte[]` and never written. Flushing them copied that uninitialised heap over the
// camera matrices the renderer had written one call earlier, and the probe scene rendered as bare sky.
//
// The remedy that shipped was a hand-maintained set of buffers it was safe to flush. That is two places
// obliged to agree with nothing checking that they do — the exact defect class Docs/Clouds/
// DEV_CONTRACT.md 2.3.1 is about. So the relation asserted here is the one that replaces it:
//
//   1. HOW A BUFFER IS FILLED IS PART OF THE BUFFER, claimed by its first write and never changed.
//   2. THE CONTRADICTING OPERATION IS REFUSED, WRITING NOTHING — not "usually harmless", not "guarded
//      by a list somewhere else": zero writes reach the buffer, and the bytes already in it survive.
//   3. A WHOLE-FILLED BUFFER REPORTS NO DIRTY FIELDS, which is what makes the four generic
//      "flush every UB with dirty fields" loops in the engine safe without any of them knowing a thing
//      about which buffers they are looking at.
//
// No GPU: BufferFillKind.hpp is a pure enum relation, UniformBufferProperty and FieldProperty are
// header-only, and ShaderResources::UniformBuffer is abstract, so the buffer here records into a
// std::vector. EngineContext and FrameManager are plain counter singletons.

#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/FrameManager.hpp>
#include <Engine/Graphic/Materials/Properties/UniformBufferProperty.hpp>
#include <Engine/ShaderResources/BufferCopyLayout.hpp>
#include <Engine/ShaderResources/BufferFillKind.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

using namespace Desert;
using namespace Desert::ShaderResources;

namespace
{
    constexpr uint32_t kFramesInFlight = 3; // what the editor's swapchain actually runs
    constexpr uint32_t kSlots          = Engine::kMaxRendererSlots;

    // Two mat4s and a vec4 — the shape of CameraUB, which is the block the defect destroyed.
    constexpr uint32_t kMat4  = 16 * sizeof( float );
    constexpr uint32_t kVec4  = 4 * sizeof( float );
    constexpr uint32_t kBytes = kMat4 * 2 + kVec4;

    // Records both the bytes AND the number of writes. The write COUNT is what makes "refused" testable
    // without depending on the contents of uninitialised memory: a flush that is refused issues zero
    // SetData calls, whereas the code that shipped issued one per dirty field.
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
            ++m_Writes;
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

        const std::vector<std::byte>& Copy( uint32_t frame, uint32_t slot ) const
        {
            return m_Copies[BufferCopyIndex( frame, slot, kSlots )];
        }

        uint32_t Writes() const
        {
            return m_Writes;
        }
        void ResetWrites()
        {
            m_Writes = 0;
        }

        static uint32_t CurrentCopy()
        {
            return BufferCopyIndex( Engine::FrameManager::GetInstance().GetCurrentFrameIndex(),
                                    EngineContext::GetInstance().GetActiveRendererSlot(), kSlots );
        }

    private:
        std::vector<std::vector<std::byte>> m_Copies;
        uint32_t                            m_Writes = 0;
    };

    // The engine-filled shape: three fields, written whole by the renderer, never through FieldProperty.
    ShaderLayout::UniformBuffer CameraModel()
    {
        ShaderLayout::UniformBuffer model;
        model.Name         = "CameraUB";
        model.BindingPoint = 0;
        model.Size         = kBytes;
        model.Fields.push_back( { Core::Formats::ShaderValueType::Mat4, "Projection", kMat4, 0, 1 } );
        model.Fields.push_back( { Core::Formats::ShaderValueType::Mat4, "View", kMat4, kMat4, 1 } );
        model.Fields.push_back( { Core::Formats::ShaderValueType::Float4, "CameraPos", kVec4, kMat4 * 2, 1 } );
        return model;
    }

    // The parameter-carrying shape: one field an artist authors.
    ShaderLayout::UniformBuffer MaterialModel()
    {
        ShaderLayout::UniformBuffer model;
        model.Name         = "MaterialUB";
        model.BindingPoint = 1;
        model.Size         = kVec4;
        model.Fields.push_back( { Core::Formats::ShaderValueType::Float4, "Tint", kVec4, 0, 1 } );
        return model;
    }

    // A camera payload distinguishable from both zeros and any plausible garbage.
    std::vector<std::byte> CameraPayload()
    {
        std::vector<float> v( kBytes / sizeof( float ) );
        for ( size_t i = 0; i < v.size(); ++i )
            v[i] = 100.0f + static_cast<float>( i );

        std::vector<std::byte> out( kBytes );
        std::memcpy( out.data(), v.data(), kBytes );
        return out;
    }

    class BufferFill : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            EngineContext::CreateInstance();
            Engine::FrameManager::CreateInstance().Initialize( kFramesInFlight );
            EngineContext::GetInstance().SetActiveRendererSlot( 0 );
        }
    };
} // namespace

// ---------------------------------------------------------------------------------------------------
// The relation itself, with no buffer in sight.
// ---------------------------------------------------------------------------------------------------

TEST( FillClaim, AnUnwrittenBufferAcceptsEitherRouteAndBecomesIt )
{
    for ( const auto requested : { FillKind::Whole, FillKind::Fields } )
    {
        const auto claim = ClaimFill( FillKind::Unclaimed, requested );
        EXPECT_TRUE( claim.Accepted );
        EXPECT_EQ( claim.Next, requested );
    }
}

TEST( FillClaim, AClaimedBufferAcceptsItsOwnRouteForever )
{
    for ( const auto kind : { FillKind::Whole, FillKind::Fields } )
    {
        const auto claim = ClaimFill( kind, kind );
        EXPECT_TRUE( claim.Accepted );
        EXPECT_EQ( claim.Next, kind ) << "a repeat write must not move the buffer";
    }
}

// The load-bearing one: refusal must also be inert. A claim that refused but still advanced the state
// would let the SECOND contradicting write through, which is worse than never having guarded at all.
TEST( FillClaim, TheOtherRouteIsRefusedAndTheStateIsUntouched )
{
    const auto whole = ClaimFill( FillKind::Whole, FillKind::Fields );
    EXPECT_FALSE( whole.Accepted );
    EXPECT_EQ( whole.Next, FillKind::Whole );

    const auto fields = ClaimFill( FillKind::Fields, FillKind::Whole );
    EXPECT_FALSE( fields.Accepted );
    EXPECT_EQ( fields.Next, FillKind::Fields );
}

// "Unclaimed" is a state a buffer is in, not a way of writing one. Asking for it is a bug in the
// caller, and answering "granted" would un-claim a buffer that had already chosen.
TEST( FillClaim, UnclaimedIsNeverAValidRequest )
{
    for ( const auto current : { FillKind::Unclaimed, FillKind::Whole, FillKind::Fields } )
    {
        const auto claim = ClaimFill( current, FillKind::Unclaimed );
        EXPECT_FALSE( claim.Accepted );
        EXPECT_EQ( claim.Next, current );
    }
}

// ---------------------------------------------------------------------------------------------------
// The defect, on the buffer.
// ---------------------------------------------------------------------------------------------------

// The reproduction. A renderer writes the camera whole; a generic "flush the fields" pass arrives one
// call later, as MaterialDeferredLighting's and Material::Bind's loops genuinely did. It must write
// NOTHING, and the matrices must still be there.
TEST_F( BufferFill, FlushingTheFieldsOfAWholeFilledBufferWritesNothing )
{
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( CameraModel() );
    Graphic::UniformBufferProperty prop( buffer );

    const auto camera = CameraPayload();
    prop.SetRawData( camera.data(), camera.size() );
    ASSERT_EQ( buffer->Copy( 0, 0 ), camera ) << "the whole-block write itself did not land";

    buffer->ResetWrites();
    prop.UpdateFields(); // the call that emptied the frame

    EXPECT_EQ( buffer->Writes(), 0u )
         << "a whole-filled buffer accepted a field flush; those field bytes are uninitialised heap";
    EXPECT_EQ( buffer->Copy( 0, 0 ), camera ) << "the camera matrices were overwritten";
    EXPECT_EQ( prop.GetFillKind(), FillKind::Whole ) << "the refused flush moved the buffer's route";
}

// The half that keeps the generic loops honest without any of them knowing what they are holding.
// Both halves matter: a buffer that has not chosen yet DOES report dirty fields (every FieldProperty is
// born dirty), which is precisely how the loops reached CameraUB in the first place.
TEST_F( BufferFill, ABufferNobodyHasWrittenReportsNoDirtyFields )
{
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( CameraModel() );
    Graphic::UniformBufferProperty prop( buffer );

    // This assertion is the REVERSE of the one that shipped here first, and the reversal is the whole
    // point. Fields used to be born dirty, so a buffer that merely DECLARED fields answered yes — and
    // Material::Bind's generic flush therefore wrote them, which claimed the buffer as field-filled, and
    // the engine's own whole-block write of CameraUB was refused by name a moment later. Every
    // shader-graph mesh vanished from the frame while every suite here stayed green, because the suite
    // asserted the defect.
    //
    // A field nobody has written has nothing to say. That is what makes the flush loops safe without a
    // list of exceptions, and it is what lets an unclaimed buffer still accept the whole-block write it
    // is waiting for.
    EXPECT_FALSE( prop.HasDirtyFields() ) << "an unwritten field must not invite a flush";

    // Acceptance is checked through the buffer rather than a return value, because SetRawData returns
    // void: a refusal is silent to the caller and only the bytes tell the truth.
    const auto camera = CameraPayload();
    prop.SetRawData( camera.data(), camera.size() );
    EXPECT_EQ( 0, std::memcmp( buffer->GetData(), camera.data(), camera.size() ) )
         << "an unclaimed buffer must accept the engine's whole-block write";

    EXPECT_FALSE( prop.HasDirtyFields() )
         << "the engine's four flush loops would pick this buffer up and destroy it";
}

TEST_F( BufferFill, WritingAFieldIsWhatMakesABufferDirty )
{
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( MaterialModel() );
    Graphic::UniformBufferProperty prop( buffer );

    ASSERT_FALSE( prop.HasDirtyFields() );

    const float tint[4] = { 0.4f, 0.5f, 0.6f, 1.0f };
    ASSERT_TRUE( prop.WriteField( prop.GetField( "Tint" ), tint, sizeof( tint ) ) );

    EXPECT_TRUE( prop.HasDirtyFields() ) << "a written field owes the GPU a flush";
}

// The mirror image: a buffer an artist's parameters own must not be blown away by a whole-block write.
TEST_F( BufferFill, AFieldFilledBufferRefusesAWholeBlockWrite )
{
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( MaterialModel() );
    Graphic::UniformBufferProperty prop( buffer );

    const float tint[4] = { 0.9f, 0.12f, 0.08f, 1.0f };
    ASSERT_TRUE( prop.WriteField( prop.GetField( "Tint" ), tint, sizeof( tint ) ) );
    prop.UpdateFields();

    std::vector<std::byte> authored( kVec4 );
    std::memcpy( authored.data(), tint, kVec4 );
    ASSERT_EQ( buffer->Copy( 0, 0 ), authored );

    const std::vector<std::byte> intruder( kVec4, std::byte{ 0x7F } );
    buffer->ResetWrites();
    prop.SetRawData( intruder.data(), intruder.size() );

    EXPECT_EQ( buffer->Writes(), 0u ) << "a whole-block write was accepted over authored parameters";
    EXPECT_EQ( buffer->Copy( 0, 0 ), authored );
    EXPECT_EQ( prop.GetFillKind(), FillKind::Fields );
}

// A field write claims the route on its own. Without this, a material whose parameters were written but
// not yet flushed would still look unclaimed, and the next whole-block write would be let through.
TEST_F( BufferFill, WritingAFieldClaimsTheRouteBeforeAnyFlush )
{
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( MaterialModel() );
    Graphic::UniformBufferProperty prop( buffer );

    EXPECT_EQ( prop.GetFillKind(), FillKind::Unclaimed );

    const float tint[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    ASSERT_TRUE( prop.WriteField( prop.GetField( "Tint" ), tint, sizeof( tint ) ) );

    EXPECT_EQ( prop.GetFillKind(), FillKind::Fields ) << "claimed only on flush leaves a window open";
}

// The pair (buffer, field) comes from Material::FindFieldInAnyUB, and a mismatched pair would claim one
// buffer's route while writing another buffer's bytes.
TEST_F( BufferFill, AFieldFromAnotherBufferIsRefused )
{
    auto                           cameraBuf = std::make_shared<RecordingUniformBuffer>( CameraModel() );
    auto                           matBuf    = std::make_shared<RecordingUniformBuffer>( MaterialModel() );
    Graphic::UniformBufferProperty camera( cameraBuf );
    Graphic::UniformBufferProperty material( matBuf );

    const float tint[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    EXPECT_FALSE( camera.WriteField( material.GetField( "Tint" ), tint, sizeof( tint ) ) );
    EXPECT_EQ( camera.GetFillKind(), FillKind::Unclaimed ) << "a foreign field claimed the route";
}

// The shadow copy of a field nobody has written is ZERO, not whatever the heap last held. The fill-kind
// refusal is what keeps those bytes off the GPU; this is the second line, and it is the difference
// between a wrong frame that is the same every run and one that is not.
TEST_F( BufferFill, AnUnwrittenFieldsShadowCopyIsZero )
{
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( CameraModel() );
    Graphic::UniformBufferProperty prop( buffer );

    for ( const char* name : { "Projection", "View", "CameraPos" } )
    {
        const auto* field = prop.GetField( name );
        ASSERT_NE( field, nullptr ) << name;

        const auto& local = field->GetLocalData();
        ASSERT_NE( local.Data, nullptr ) << name;

        const auto* bytes = static_cast<const std::byte*>( local.Data );
        for ( size_t i = 0; i < local.GetAllocatedSize(); ++i )
            ASSERT_EQ( bytes[i], std::byte{ 0 } ) << name << " byte " << i << " is uninitialised heap";
    }
}

// A storage buffer has no fields at all, which is what makes it a single-route buffer. The body used to
// be `return {}` — a reference bound to a temporary that died at the closing brace — so this asserts the
// reference is to something that outlives the call, not merely that it is empty.
TEST( StorageBufferFields, HasNoneAndTheReferenceOutlivesTheCall )
{
    class Stub final : public StorageBuffer
    {
    public:
        void SetData( const void*, uint32_t, uint32_t ) override
        {
        }
        uint8_t* MapMemory() override
        {
            return nullptr;
        }
        void UnmapMemory() override
        {
        }
        const uint32_t GetBinding() const override
        {
            return 0;
        }
        const uint32_t GetSize() const override
        {
            return 0;
        }
        const void* GetData() const override
        {
            return nullptr;
        }
    };

    Stub buffer;
    EXPECT_TRUE( buffer.GetFields().empty() ) << "a storage buffer with fields would have two fill routes";

    // Asked from two different stack depths. A `static` answers with the same address both times; a
    // temporary is materialised in the CALLER's frame, so its address moves with the frame.
    //
    // Comparing two calls at the SAME depth does not work, and that is a hole this suite had: the
    // sabotage that put `return {}` back went green, because both temporaries landed on the same stack
    // slot. Neither did the compiler object — clang issues no diagnostic for a reference bound to a
    // temporary returned through a `const&` return type. Recursion is what makes the difference real.
    struct Probe
    {
        static const void* At( const StorageBuffer& b, int depth )
        {
            if ( depth > 0 )
                return At( b, depth - 1 );
            return &b.GetFields();
        }
    };

    EXPECT_EQ( Probe::At( buffer, 0 ), Probe::At( buffer, 24 ) )
         << "GetFields() handed back something that lives on the caller's stack";
}

// Whole-block writes stay repeatable — the renderer restates the camera every frame and every slot, and
// a guard that only let the first one through would be a far worse bug than the one it replaced.
TEST_F( BufferFill, TheClaimedRouteRemainsUsableEveryFrameAndEverySlot )
{
    auto                           buffer = std::make_shared<RecordingUniformBuffer>( CameraModel() );
    Graphic::UniformBufferProperty prop( buffer );

    const auto camera = CameraPayload();
    for ( uint32_t slot = 0; slot < kSlots; ++slot )
    {
        EngineContext::GetInstance().SetActiveRendererSlot( slot );
        for ( uint32_t f = 0; f < kFramesInFlight; ++f )
        {
            prop.SetRawData( camera.data(), camera.size() );
            Engine::FrameManager::GetInstance().NextFrame();
        }
    }
    EngineContext::GetInstance().SetActiveRendererSlot( 0 );

    for ( uint32_t slot = 0; slot < kSlots; ++slot )
        for ( uint32_t f = 0; f < kFramesInFlight; ++f )
            EXPECT_EQ( buffer->Copy( f, slot ), camera )
                 << "frame " << f << " slot " << slot << " never received the camera";
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
