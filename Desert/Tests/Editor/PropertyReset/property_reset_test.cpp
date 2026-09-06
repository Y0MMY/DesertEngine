// The reset-to-default RELATION, asserted end to end at the byte level (Д29).
//
// The owner reported "Reset to default не работает" twice, and both times every individual piece read
// correctly: the button set its flag, the flag reached the return, the default instance existed. What
// was broken was the relation — the value moved in memory while every mechanism that makes an edit
// REAL (undo, the unsaved-changes revision, the multi-select broadcast) learned nothing, because the
// report was produced and then dropped one link later. So this file tests relations, not functions:
//
//   1. after a reset, the field's bytes EQUAL the same field of the type's default instance;
//   2. the caller LEARNS of the change (the return), and the history's revision moves with it —
//      a reset that reports false must leave the revision alone, one that reports true must not;
//   3. undo restores the exact pre-reset bytes (the recorded edit is the real one);
//   4. reset-then-broadcast lands every selected object on the default (the multi-select path).
//
// ResetFieldToDefault + MultiEdit are std-only, so the whole decision compiles here without ImGui, a
// window or a GPU. What this file CANNOT reach is the ImGui plumbing around it (button visibility,
// DrawMulti handing the default instance through) — that part is only provable by driving the editor
// and looking, which Д29 did; extracting the rule makes the DECISION reachable, not the file.

#include <gtest/gtest.h>

#include <Editor/Core/CommandHistory.hpp>
#include <Editor/Core/MultiEdit.hpp>
#include <Editor/Panels/PropertyEditor/PropertyReset.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

using Desert::Editor::AnyFieldDiffers;
using Desert::Editor::BroadcastField;
using Desert::Editor::CommandHistory;
using Desert::Editor::ResetFieldToDefault;
using Desert::Reflection::FieldInfo;
using Desert::Reflection::FieldType;

namespace
{
    // A stand-in reflected component: member initializers are the "factory defaults", exactly like the
    // types TypeBuilder::WithDefault<T>() serves to the editor.
    struct CloudLike
    {
        bool  Enabled  = true;
        float Coverage = 0.5f;
        int   Seed     = 7;
    };

    FieldInfo Field( const char* name, FieldType type, std::size_t offset, std::size_t size )
    {
        FieldInfo f;
        f.Name   = name;
        f.Type   = type;
        f.Offset = offset;
        f.Size   = size;
        return f;
    }

    const FieldInfo kEnabled =
         Field( "Enabled", FieldType::Bool, offsetof( CloudLike, Enabled ), sizeof( CloudLike::Enabled ) );
    const FieldInfo kCoverage =
         Field( "Coverage", FieldType::Float, offsetof( CloudLike, Coverage ), sizeof( CloudLike::Coverage ) );
    const FieldInfo kSeed =
         Field( "Seed", FieldType::Int, offsetof( CloudLike, Seed ), sizeof( CloudLike::Seed ) );

    bool FieldEqualsDefault( const CloudLike& obj, const CloudLike& def, const FieldInfo& f )
    {
        return std::memcmp( reinterpret_cast<const std::byte*>( &obj ) + f.Offset,
                            reinterpret_cast<const std::byte*>( &def ) + f.Offset, f.Size ) == 0;
    }
} // namespace

TEST( PropertyReset, ResetFieldEqualsTheDefaultInstanceField )
{
    const CloudLike def{};
    CloudLike       obj;
    obj.Enabled  = false;
    obj.Coverage = 0.9f;

    CommandHistory history;

    // The relation, per field: after the reset the field's bytes are the default instance's bytes.
    EXPECT_TRUE( ResetFieldToDefault( &obj, kEnabled, &def, history ) );
    EXPECT_TRUE( FieldEqualsDefault( obj, def, kEnabled ) );

    EXPECT_TRUE( ResetFieldToDefault( &obj, kCoverage, &def, history ) );
    EXPECT_TRUE( FieldEqualsDefault( obj, def, kCoverage ) );

    // Only the reset field moves: Seed was never touched and never reset.
    EXPECT_EQ( obj.Seed, 7 );
}

TEST( PropertyReset, TheCallerLearnsAndTheRevisionMovesTogether )
{
    const CloudLike def{};
    CloudLike       obj;
    obj.Coverage = 0.9f;

    CommandHistory history;
    const uint64_t before = history.Revision();

    // A real change: reported true, revision moved — this is the notification the owner's click lost
    // twice (first inside DrawField, then in the callers that dropped the return).
    EXPECT_TRUE( ResetFieldToDefault( &obj, kCoverage, &def, history ) );
    EXPECT_EQ( history.Revision(), before + 1 );

    // A no-op reset (already at the default): reported false, revision UNMOVED. "Unsaved changes"
    // must mean a value moved, not that a button was pressed.
    EXPECT_FALSE( ResetFieldToDefault( &obj, kCoverage, &def, history ) );
    EXPECT_EQ( history.Revision(), before + 1 );
}

TEST( PropertyReset, UndoRestoresTheExactPreResetBytes )
{
    const CloudLike def{};
    CloudLike       obj;
    obj.Coverage = 0.9f;

    CommandHistory history;
    ASSERT_TRUE( ResetFieldToDefault( &obj, kCoverage, &def, history ) );
    ASSERT_FLOAT_EQ( obj.Coverage, def.Coverage );

    // The recorded edit is the real one: undo brings back what the user had, redo the default.
    EXPECT_TRUE( history.Undo() );
    EXPECT_FLOAT_EQ( obj.Coverage, 0.9f );
    EXPECT_TRUE( history.Redo() );
    EXPECT_FLOAT_EQ( obj.Coverage, def.Coverage );
}

TEST( PropertyReset, RefusesWithoutADefaultInstance )
{
    CloudLike obj;
    obj.Coverage = 0.9f;
    CommandHistory history;

    // No default instance -> no reset, no report, no revision: the exact combination DrawMulti used
    // to create for every multi-selection by passing nullptr (Д29, defect 2).
    EXPECT_FALSE( ResetFieldToDefault( &obj, kCoverage, nullptr, history ) );
    EXPECT_FLOAT_EQ( obj.Coverage, 0.9f );
    EXPECT_EQ( history.Revision(), 0u );
}

TEST( PropertyReset, ResetThenBroadcastLandsTheWholeSelectionOnTheDefault )
{
    const CloudLike def{};
    CloudLike       primary, otherA, otherB;
    primary.Coverage = 0.9f;
    otherA.Coverage  = 0.7f;
    otherB.Coverage  = 0.3f;

    std::vector<void*> others{ &otherA, &otherB };
    ASSERT_TRUE( AnyFieldDiffers( &primary, others, kCoverage.Offset, kCoverage.Size ) );

    CommandHistory history;

    // What DrawMulti's field loop does with the report: reset the primary, then broadcast the field.
    ASSERT_TRUE( ResetFieldToDefault( &primary, kCoverage, &def, history ) );
    BroadcastField( &primary, others, kCoverage.Offset, kCoverage.Size );

    EXPECT_TRUE( FieldEqualsDefault( primary, def, kCoverage ) );
    EXPECT_TRUE( FieldEqualsDefault( otherA, def, kCoverage ) );
    EXPECT_TRUE( FieldEqualsDefault( otherB, def, kCoverage ) );
    // The broadcast copies ONLY that field — the others keep their own values elsewhere.
    EXPECT_EQ( otherA.Seed, 7 );
}

TEST( PropertyReset, MixedSelectionWithPrimaryAtDefaultStillReachesTheOthers )
{
    // The edge the button used to hide: the PRIMARY already sits at the default while the others do
    // not. The reset on the primary is a no-op (false, nothing recorded) — but the row must still
    // report upward (DrawField ORs in `mixed`) so the broadcast runs; this asserts the broadcast half
    // of that relation.
    const CloudLike def{};
    CloudLike       primary; // at the default
    CloudLike       other;
    other.Coverage = 0.7f;

    std::vector<void*> others{ &other };
    ASSERT_TRUE( AnyFieldDiffers( &primary, others, kCoverage.Offset, kCoverage.Size ) );

    CommandHistory history;
    EXPECT_FALSE( ResetFieldToDefault( &primary, kCoverage, &def, history ) );

    BroadcastField( &primary, others, kCoverage.Offset, kCoverage.Size );
    EXPECT_TRUE( FieldEqualsDefault( other, def, kCoverage ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
