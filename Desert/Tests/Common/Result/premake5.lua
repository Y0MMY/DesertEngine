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

    for _, link in ipairs(deps.TestSpecific.Libraries) do
        links {link }
    end
    
    
    for _, define in ipairs(deps.TestSpecific.Defines) do
        defines { define }
    end
    
    links {
        "Common",
        "%{wks.location}/ThirdParty/google-test/bin/gtestd.lib"
    }
    
print("Configured test project: " .. test_name)