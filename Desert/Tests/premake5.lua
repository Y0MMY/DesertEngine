local currentDir = _MAIN_SCRIPT_DIR

os.mkdir( currentDir .. "/build/TestReports")

local test_files = os.matchfiles("**/*_test.cpp")

group "Tests"
    project "BuildAllTests"
        kind "Utility"
        targetdir "%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}"
	    objdir "%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}"
        
        for _, test_file in ipairs(test_files) do
            local project_name = path.getbasename(test_file)
           -- dependson(project_name)
        end

    project "RunAllTests"
        kind "Utility"
        
        postbuildcommands {
            "if exist \"%{wks.location}\\run_tests.bat\" del \"%{wks.location}\\run_tests.bat\"",
            
            "echo @echo off > \"%{wks.location}\\run_tests.bat\"",
            "echo setlocal enabledelayedexpansion >> \"%{wks.location}\\run_tests.bat\"",
            "echo set TEST_DIR=%{wks.location}\\build\\Bin\\Tests\\%{cfg.buildcfg}>> \"%{wks.location}\\run_tests.bat\"",
            "echo set REPORT_DIR=%{wks.location}\\build\\TestReports>> \"%{wks.location}\\run_tests.bat\"",
            "echo if not exist \"!REPORT_DIR!\" mkdir \"!REPORT_DIR!\">> \"%{wks.location}\\run_tests.bat\"",
            "echo set ERROR='0'>> \"%{wks.location}\\run_tests.bat\"",
            "echo echo ===== Starting Tests =====>> \"%{wks.location}\\run_tests.bat\"",
        }
        
        for _, test_file in ipairs(test_files) do
            local project_name = path.getbasename(test_file)
            postbuildcommands {
                "echo echo [TEST] !TEST_DIR!\\"..project_name..".exe>> \"%{wks.location}\\run_tests.bat\"",
                "echo if exist \"!TEST_DIR!\\"..project_name..".exe\" (>> \"%{wks.location}\\run_tests.bat\"",
                "echo   \"!TEST_DIR!\\"..project_name..".exe\" --gtest_output=xml:\"!REPORT_DIR!\\"..project_name..".xml\">> \"%{wks.location}\\run_tests.bat\"",
                "echo   if !ERRORLEVEL! NEQ 0 (>> \"%{wks.location}\\run_tests.bat\"",
                "echo     echo [FAIL] "..project_name..">> \"%{wks.location}\\run_tests.bat\"",
                "echo     set ERROR='1'>> \"%{wks.location}\\run_tests.bat\"",
                "echo   )>> \"%{wks.location}\\run_tests.bat\"",
                "echo ) else (>> \"%{wks.location}\\run_tests.bat\"",
                "echo   echo [ERROR] "..project_name..".exe not found>> \"%{wks.location}\\run_tests.bat\"",
                "echo   set ERROR='1'>> \"%{wks.location}\\run_tests.bat\"",
                "echo )>> \"%{wks.location}\\run_tests.bat\"",
            }
        end
        
        postbuildcommands {
            "echo echo ===== Test Results =====>> \"%{wks.location}\\run_tests.bat\"",
            "echo if !ERROR! == '0' (>> \"%{wks.location}\\run_tests.bat\"",
            "echo   echo ALL TESTS PASSED>> \"%{wks.location}\\run_tests.bat\"",
            "echo ) else (>> \"%{wks.location}\\run_tests.bat\"",
            "echo   echo SOME TESTS FAILED>> \"%{wks.location}\\run_tests.bat\"",
            "echo )>> \"%{wks.location}\\run_tests.bat\"",
            "echo exit /b !ERROR!>> \"%{wks.location}\\run_tests.bat\"",
            
            "call \"%{wks.location}\\run_tests.bat\""
        }

function createTestProject(test_file)
    local project_name = path.getbasename(test_file)
    
    project(project_name)
        kind "ConsoleApp"

        targetdir ("%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}")
        objdir ("%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}")
        
        files { 
            test_file,
            "Tests/TestUtils/**.cpp",
            "Tests/TestUtils/**.hpp"
        }
        
        includedirs {
            "../Desert/Source",
            "../Common/Source",
            "Tests/TestUtils",
        }
        
        for _, path in pairs(deps.Common.IncludeDir) do
            includedirs { path }
        end
        for _, path in pairs(deps.TestSpecific.IncludeDir) do
            includedirs { path }
        end
        
        links {
            "Common",
            "Desert",
            "ImGui",
        }
        
        filter "configurations:Debug"
            defines {
                "DESERT_CONFIG_DEBUG",
                "DEBUG",
                "_DEBUG"
            }
            runtime "Debug"
            symbols "On"
            
            for _, path in pairs(deps.TestSpecific.Libraries.Debug) do
                links { path }
            end
            
            -- postbuildcommands {
            --     "{COPY} \"%{wks.location}/ThirdParty/google-test/bin/Debug/*.dll\" \"%{cfg.buildtarget.directory}\""
            -- }
        
        filter "configurations:Release"
            defines { "DESERT_CONFIG_RELEASE" }
            runtime "Release"
            optimize "On"
            
            for _, path in pairs(deps.TestSpecific.Libraries.Release) do
                links { path }
            end
            
            -- postbuildcommands {
            --     "{COPY} \"%{wks.location}/ThirdParty/google-test/bin/Release/*.dll\" \"%{cfg.buildtarget.directory}\""
            -- }
        
        filter "system:windows"
            system "windows"
            defines { "DESERT_PLATFORM_WINDOWS" }
            links { "shlwapi.lib" }
            
        filter "system:linux"
            system "linux"
            defines { "DESERT_PLATFORM_LINUX" }
            links { "pthread" }
end

for _, test_file in ipairs(test_files) do
    createTestProject(test_file)
end

print("\n=== Test Configuration ===")
print("Generated test projects: " .. #test_files)
for i, file in ipairs(test_files) do
    print("  " .. i .. ". " .. path.getbasename(file))
end
print("Test reports will be saved to: ".. currentDir .. "/build/TestReports")