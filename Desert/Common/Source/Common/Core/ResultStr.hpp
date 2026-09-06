#pragma once

// The definition moved to the desert-shared submodule (L2 design §7 item 3): the shared-format API
// is written in this type, and the launcher must consume it without the engine. This spelling stays
// because ~a hundred engine files include <Common/Core/ResultStr.hpp>; the definition (namespace
// Common:: and all) now has exactly one home. The include is RELATIVE on purpose — every project in
// this workspace compiles files that reach this header, and a <DesertShared/...> spelling here
// would have obliged every one of their generated makefiles to carry the submodule include path.
//
// Empty ThirdParty/desert-shared ⇒ `git submodule update --init ThirdParty/desert-shared`
// (a recorded gitlink checks out nothing by itself — see the submodule's README).
#include "../../../../../ThirdParty/desert-shared/Include/DesertShared/ResultStr.hpp"
