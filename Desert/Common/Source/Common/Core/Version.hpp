#pragma once

#include <cstdint>
#include <optional>

namespace Common::Version
{
    // Build/version identity, derived from git BY THE BUILD. The `Common` project runs
    // scripts/GenVersion.sh (scripts\Windows\GenVersion.bat under MSBuild), which writes
    // Version.gen.hpp; only the single Version.cpp TU includes it, so a version bump never triggers a
    // wide rebuild. Format of Full(): "<base>.<commits>+<hash>[.dirty]" — e.g. "0.1.1236+09e6d3b8",
    // where <base> comes from the repo-root VERSION file and <commits> is `git rev-list --count HEAD`
    // (a monotonically growing build number, UE-changelist style).

    const char* Full();   // "0.1.1236+09e6d3b8" / "0.1.1236+09e6d3b8.dirty" / "0.1+unknown"
    const char* Base();   // "0.1"
    const char* Hash();   // "09e6d3b8", or the literal "unknown" — no abbreviated hash can spell that
    const char* Branch(); // "dev", or the literal "unknown" — no branch can be named that either

    // THE BUILD NUMBER IS ABSENT, NOT ZERO, when git could not supply a trustworthy one: no
    // repository at all (a source archive), or a shallow clone, where `rev-list --count` answers 1 and
    // means nothing. It is `optional` and not a sentinel because this number gets COMPARED — the
    // launcher gates a collection on `EngineMinCommit` against it — and a zero standing for "unknown"
    // reads as "an ancient build", which silently fails every threshold instead of saying it cannot
    // answer. One value for two meanings is this project's most repeated defect; here the second
    // meaning has no value at all, and the caller is made to decide what unknown means to it.
    std::optional<std::uint32_t> CommitCount(); // 1236

    // Uncommitted tracked changes at build time. False when there is no git HEAD, which is not a
    // second meaning: with nothing to be dirty against, "no local modifications" is the truth, and
    // Full() already says "+unknown" so nobody reads that false as a clean checkout of a known commit.
    bool Dirty();
} // namespace Common::Version
