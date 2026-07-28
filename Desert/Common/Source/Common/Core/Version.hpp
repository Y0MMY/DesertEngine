#pragma once

#include <cstdint>

namespace Common::Version
{
    // Build/version identity, derived from git at build time (scripts/GenVersion.sh writes
    // Version.gen.hpp; only the single Version.cpp TU includes it, so a version bump never triggers
    // a wide rebuild). Format of Full(): "<base>.<commits>+<hash>[.dirty]" — e.g. "0.1.316+db43fdb",
    // where <base> comes from the repo-root VERSION file and <commits> is `git rev-list --count HEAD`
    // (a monotonically growing build number, UE-changelist style).

    const char* Full();        // "0.1.316+db43fdb" / "0.1.316+db43fdb.dirty"
    const char* Base();        // "0.1"
    const char* Hash();        // "db43fdb"
    const char* Branch();      // "dev"
    uint32_t    CommitCount(); // 316
    bool        Dirty();       // uncommitted tracked changes at build time
} // namespace Common::Version
