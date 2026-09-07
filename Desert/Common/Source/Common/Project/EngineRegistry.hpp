#pragma once

// `~/.desertengine/engines.json` lives in the desert-shared submodule for the same reason the
// project formats do: two processes have to understand it identically, and after L3 they are two
// repositories. The ENGINE is the writer here (it registers its own root and version at Editor
// startup) and the launcher is the reader — the one shared format whose direction runs that way.
//
// This spelling exists so engine code can include it without every generated makefile carrying the
// submodule include path; the relative include is the same trick as Common/Project/ProjectFormat.hpp.
//
// Empty ThirdParty/desert-shared ⇒ `git submodule update --init ThirdParty/desert-shared`.
#include "../../../../../ThirdParty/desert-shared/Include/DesertShared/EngineRegistry.hpp"
