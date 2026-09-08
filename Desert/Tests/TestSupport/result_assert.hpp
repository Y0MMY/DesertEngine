#pragma once

// ASSERTING ON THE VALUE OF A RESULT, WITHOUT THROWING AWAY WHY IT FAILED.
//
// The shape this replaces was everywhere in the suites:
//
//     EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( p ).GetValue(), "loose-a" );
//
// It reads well and it is a trap twice over. First, it unwraps a TEMPORARY, so there is no variable
// anyone could have checked — Ф4 deleted the rvalue overload precisely because that form is
// unguardable by construction, which is why these lines stopped compiling. Second, and this is the
// part that mattered before the compiler ever objected: when the read FAILED, `GetValue()` handed back
// a default-constructed `std::string` and the assertion reported
//
//     Expected equality of these values:  ""  vs  "loose-a"
//
// A test that fails saying "I got an empty string" when the truth is "the pak was never mounted, and
// here is the error the mount returned" costs its reader the whole debugging session. The failure's own
// message was sitting in the result and was discarded one method call earlier.
//
// So the fix is not a mechanical `auto r = expr;` at every site. These macros name the result, ASSERT
// on success while printing the error text, and only then compare — the failure says what actually
// went wrong, and the value is unreachable until success is established.

#include <gtest/gtest.h>

// Assert @p expr succeeded, then compare its value with @p expected.
#define DESERT_EXPECT_RESULT_EQ( expr, expected )                                                                 \
    do                                                                                                            \
    {                                                                                                             \
        auto desertResult_ = ( expr );                                                                            \
        ASSERT_TRUE( desertResult_.IsSuccess() ) << #expr " failed: " << desertResult_.GetError();                \
        EXPECT_EQ( desertResult_.GetValue(), expected );                                                          \
    } while ( false )

// Assert @p expr succeeded and hand the value to @p stmt as `value`. For the cases that do something
// other than compare — a substring search, a size check, a second parse.
#define DESERT_WITH_RESULT( expr, stmt )                                                                          \
    do                                                                                                            \
    {                                                                                                             \
        auto desertResult_ = ( expr );                                                                            \
        ASSERT_TRUE( desertResult_.IsSuccess() ) << #expr " failed: " << desertResult_.GetError();                \
        const auto& value = desertResult_.GetValue();                                                             \
        stmt;                                                                                                     \
    } while ( false )
