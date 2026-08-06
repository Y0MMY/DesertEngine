-- Windows-wide workspace settings.

filter "system:windows"
    architecture "x64"
    systemversion "latest"
    -- NOMINMAX workspace-wide: <windows.h> defines min/max as MACROS, which then mangle any
    -- `std::max(...)` / `std::numeric_limits<T>::max()` reached through a later include — reflect-cpp's
    -- Result.hpp and Literal.hpp break this way (C2589 "'(' illegal token on right side of '::'").
    -- It was previously worked around with a #define in individual .cpp files, which only holds until
    -- the next translation unit picks up the same headers in a different order.
    defines { "NOMINMAX" }

filter {}
