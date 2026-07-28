#include "Version.hpp"

// The generated header exists only after scripts/GenVersion.sh ran (BuildMacOS.sh does it before
// premake). Builds without git / without the script still compile — with an "unknown" identity.
#if __has_include( "Version.gen.hpp" )
#include "Version.gen.hpp"
#else
#define DESERT_VERSION_BASE "0.0"
#define DESERT_VERSION_COMMITS 0u
#define DESERT_VERSION_HASH "unknown"
#define DESERT_VERSION_BRANCH "unknown"
#define DESERT_VERSION_DIRTY 0
#endif

#include <cstdio>

namespace Common::Version
{
    const char* Full()
    {
        static char s_Full[96] = {};
        if ( s_Full[0] == '\0' )
            std::snprintf( s_Full, sizeof( s_Full ), "%s.%u+%s%s", DESERT_VERSION_BASE,
                           DESERT_VERSION_COMMITS, DESERT_VERSION_HASH,
                           DESERT_VERSION_DIRTY ? ".dirty" : "" );
        return s_Full;
    }

    const char* Base()
    {
        return DESERT_VERSION_BASE;
    }

    const char* Hash()
    {
        return DESERT_VERSION_HASH;
    }

    const char* Branch()
    {
        return DESERT_VERSION_BRANCH;
    }

    uint32_t CommitCount()
    {
        return DESERT_VERSION_COMMITS;
    }

    bool Dirty()
    {
        return DESERT_VERSION_DIRTY != 0;
    }
} // namespace Common::Version
