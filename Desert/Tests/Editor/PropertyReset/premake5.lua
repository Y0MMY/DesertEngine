local test_name = path.getname(_SCRIPT_DIR)
local test_files = os.matchfiles("*.cpp")

project(test_name)
    kind "ConsoleApp"
    language "C++"

    targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
    objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")

    -- PropertyReset is the DECISION behind the Details reset button (bytes -> default + the recorded
    -- edit), deliberately std-only so it compiles here without ImGui, a window or a GPU. MultiEdit
    -- rides along for the reset-then-broadcast relation — the multi-select half of Д29.
    files {
        test_files,
        "%{wks.location}/Editor/Source/Editor/Panels/PropertyEditor/PropertyReset.cpp",
        "%{wks.location}/Editor/Source/Editor/Core/MultiEdit.cpp",
    }

    includedirs {
        "%{wks.location}/Desert/Common/Source",
        "%{wks.location}/Desert/Desert/Source",                  -- <Engine/Reflection/ReflectionTypes.hpp>
        "%{wks.location}/Editor/Source",                         -- <Editor/Panels/PropertyEditor/PropertyReset.hpp>
    }
    externalincludedirs {
        "%{wks.location}/ThirdParty/reflect-cpp/include",        -- ReflectionTypes.hpp -> <rflcpp/rfl/Generic.hpp>
    }

    for name, path in pairs(deps.Common.IncludeDir) do
        externalincludedirs { path }
    end

    for name, path in pairs(deps.TestSpecific.IncludeDir) do
        externalincludedirs { path }
    end

    for _, define in ipairs(deps.TestSpecific.Defines) do
        defines { define }
    end

    links { "Common", "Optick" }

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
