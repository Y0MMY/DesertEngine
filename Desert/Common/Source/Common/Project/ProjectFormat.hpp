#pragma once

// The definition moved to the desert-shared submodule (L2 design §7 item 1): the launcher writes
// these formats and must do so without linking or including the engine, so the structs, the
// serializer and the folder census live there — one definition for both repositories. This spelling
// stays for the engine's own consumers (ProjectContext, GamePackager, tests); the include is
// RELATIVE for the same reason as in Common/Core/ResultStr.hpp — it keeps every generated makefile
// free of the submodule include path.
//
// Empty ThirdParty/desert-shared ⇒ `git submodule update --init ThirdParty/desert-shared`.
#include "../../../../../ThirdParty/desert-shared/Include/DesertShared/ProjectFormat.hpp"
