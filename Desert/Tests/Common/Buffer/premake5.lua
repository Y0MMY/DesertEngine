local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"
    
    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")
    
    files { 
        test_files,
    }
    
    includedirs {
        "%{wks.location}/Desert/Common/Source",
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        includedirs { path }
    end
    
    for name, path in pairs(deps.TestSpecific.IncludeDir) do
        includedirs { path }
    end

    for _, define in ipairs(deps.TestSpecific.Defines) do
        defines { define }
    end

    links { "Common", "Optick" } -- Commons JobSystem registers worker threads with Optick

    -- No DESERT_PLATFORM_* block here on purpose, and it is worth saying why since every engine and
    -- tool project has one. Buffer::Write uses DESERT_VERIFY, which expands DESERT_DEBUG_BREAK; that
    -- macro's fallback branch in Core.hpp used to be MSVC's __debugbreak(), so a target naming no
    -- platform could not COMPILE this header. Core.hpp now derives the fallback from the compiler
    -- instead, which is what makes a plain test project able to include engine headers at all.

    -- gtest comes from Dependencies.lua (prebuilt .lib on Windows, Homebrew on macOS)
    filter "configurations:Debug"
        for name, path in pairs(deps.TestSpecific.Libraries.Debug) do
            links { path }
        end

    filter "configurations:Release"
        for name, path in pairs(deps.TestSpecific.Libraries.Release) do
            links { path }
        end

    filter {}
    
print("Configured test project: " .. test_name)