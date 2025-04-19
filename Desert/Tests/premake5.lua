-- Находим все файлы с суффиксом _test.cpp в проекте
local test_files = os.matchfiles("**/*_test.cpp")

-- Для каждого тестового файла создаем отдельный исполняемый проект
for _, test_file in ipairs(test_files) do
    -- Извлекаем имя проекта из имени файла (удаляя и путь, и расширение)
    local project_name = path.getbasename(test_file)
    
    project(project_name)
        kind "ConsoleApp"  
        targetdir "%{wks.location}/build/Bin/Tests/%{cfg.buildcfg}"
	    objdir "%{wks.location}/build/Tests/Intermediates/%{cfg.buildcfg}"
        
        includedirs {
            "Desert/Source",
            "Common/Source",
            "%{wks.location}/ThirdParty/google-test/include/" 
        }
        
        -- Добавляем только этот тестовый файл
        files { test_file }
        
        links {
            "Common",
            "Desert",
            "%{wks.location}/ThirdParty/google-test/bin/gtest.lib" 
        }
        
        -- Настройки для Debug конфигурации
        filter "configurations:Debug"
            defines { "DESERT_CONFIG_DEBUG" }
            runtime "Debug"
            symbols "On"
            
        -- Настройки для Release конфигурации
        filter "configurations:Release"
            defines { "DESERT_CONFIG_RELEASE" }
            runtime "Release"
            optimize "On"
            
        -- Можно добавить фильтр для платформ, если нужно
        filter "system:windows"
            system "windows"
            
        filter "system:linux"
            system "linux"
end

-- Выводим информацию о созданных тестах (для отладки)
print("Generated test projects: " .. #test_files)
for i, file in ipairs(test_files) do
    print("  " .. i .. ". " .. path.getbasename(file))
end