local currentDir = _MAIN_SCRIPT_DIR

os.mkdir(currentDir .. "/build/TestReports")

local test_premake_files = os.matchfiles("./**/premake5.lua")

for _, premake_file in ipairs(test_premake_files) do
    include(path.getdirectory(premake_file))
end

group "Tests"
    project "BuildAllTests"
        kind "Utility"
        targetdir "%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}"
        objdir "%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}"
        
        for _, premake_file in ipairs(test_premake_files) do
            local test_dir = path.getdirectory(premake_file)
            local test_name = path.getname(test_dir)
           -- dependson(test_name)
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
        
        for _, premake_file in ipairs(test_premake_files) do
            local test_dir = path.getdirectory(premake_file)
            local test_name = path.getname(test_dir)
            postbuildcommands {
                "echo echo [TEST] !TEST_DIR!\\"..test_name..".exe>> \"%{wks.location}\\run_tests.bat\"",
                "echo if exist \"!TEST_DIR!\\"..test_name..".exe\" (>> \"%{wks.location}\\run_tests.bat\"",
                "echo   \"!TEST_DIR!\\"..test_name..".exe\" --gtest_output=xml:\"!REPORT_DIR!\\"..test_name..".xml\">> \"%{wks.location}\\run_tests.bat\"",
                "echo   if !ERRORLEVEL! NEQ 0 (>> \"%{wks.location}\\run_tests.bat\"",
                "echo     echo [FAIL] "..test_name..">> \"%{wks.location}\\run_tests.bat\"",
                "echo     set ERROR='1'>> \"%{wks.location}\\run_tests.bat\"",
                "echo   )>> \"%{wks.location}\\run_tests.bat\"",
                "echo ) else (>> \"%{wks.location}\\run_tests.bat\"",
                "echo   echo [ERROR] "..test_name..".exe not found>> \"%{wks.location}\\run_tests.bat\"",
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

print("\n=== Test Configuration ===")
print("Found test modules: " .. #test_premake_files)
for i, file in ipairs(test_premake_files) do
    print("  " .. i .. ". " .. path.getdirectory(file))
end
print("Test reports will be saved to: ".. currentDir .. "/build/TestReports")