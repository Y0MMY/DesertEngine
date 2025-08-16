local baseDir = "%{wks.location}/ThirdParty"

local function findVulkanSDK()
    print("[Vulkan] Searching for Vulkan SDK...")
    
    local vulkan_sdk = os.getenv("VULKAN_SDK")
    
    if vulkan_sdk and vulkan_sdk ~= "" then
        print("[Vulkan] Found via VULKAN_SDK environment variable: " .. vulkan_sdk)
    else
        print("[Vulkan] VULKAN_SDK environment variable not set or empty")
        
        if os.host() == "windows" then
            print("[Vulkan] Trying to find VulkanSDK in Program Files...")
            local program_files = os.getenv("PROGRAMFILES")
            vulkan_sdk = program_files .. "/VulkanSDK"
            
            -- Latest version
            local versions = os.matchdirs(vulkan_sdk .. "/*")
            table.sort(versions, function(a, b) return a > b end)
            if #versions > 0 then
                vulkan_sdk = versions[1]
                print("[Vulkan] Found in Program Files: " .. vulkan_sdk)
            else
                print("[Vulkan] No VulkanSDK versions found in " .. vulkan_sdk)
            end
        else
            -- Linux/macOS
            vulkan_sdk = "/usr/local/vulkan"
            print("[Vulkan] Trying default path for Linux/macOS: " .. vulkan_sdk)
        end
    end
    
    if not vulkan_sdk or vulkan_sdk == "" then
        print("[Vulkan] Warning: Vulkan SDK not found!")
        return nil
    end
    
    print("[Vulkan] Using Vulkan SDK at: " .. vulkan_sdk)
    return vulkan_sdk
end

local vulkan_sdk = findVulkanSDK()

local function getVulkanLibs(config)
    local libs = {}
    
    if vulkan_sdk then
        print(string.format("[Vulkan] Getting libraries for config: %s", config))
        local suffix = config:find("Debug") and "d" or ""
        
        table.insert(libs, vulkan_sdk .. "/Lib/vulkan-1.lib")
        
        table.insert(libs, vulkan_sdk .. "/Lib/shaderc" .. suffix .. ".lib")
        table.insert(libs, vulkan_sdk .. "/Lib/shaderc_shared" .. suffix .. ".lib")
        table.insert(libs, vulkan_sdk .. "/Lib/shaderc_combined" .. suffix .. ".lib")
        table.insert(libs, vulkan_sdk .. "/Lib/shaderc_util" .. suffix .. ".lib")
        
        table.insert(libs, vulkan_sdk .. "/Lib/spirv-cross-core" .. suffix .. ".lib")
        table.insert(libs, vulkan_sdk .. "/Lib/spirv-cross-glsl" .. suffix .. ".lib")
        
        table.insert(libs, vulkan_sdk .. "/Lib/OGLCompiler" .. suffix .. ".lib")
    end
    
    return libs
end

Dependencies = {
    Common = {
        IncludeDir = {
            spdlog = baseDir .. "/spdlog/include",
            yaml_cpp = baseDir .. "/yaml-cpp/include",
            glm = baseDir .. "/glm",
            GLFW = baseDir .. "/GLFW/include",
        },
        Libraries = {
            yaml_cpp = "yaml-cpp"
        },
        Defines = {
            "YAML_CPP_STATIC_DEFINE"
        }
    },
    
    CommonSpecific = {
        IncludeDir = {
        },
        Libraries = {
        },
        Defines = {
        }
    },
    
    DesertSpecific = {
        IncludeDir = {
            base = baseDir,
            stb = baseDir .. "/stb/include",
            vkallocator = baseDir .. "/VulkanAllocator",
            imgui = baseDir .. "/stb/ImGui",
            assimp = baseDir .. "/assimp/include",
            entt = baseDir .. "/entt/include",
            reflect_cpp = baseDir .. "/reflect-cpp/include",
            Vulkan = vulkan_sdk and (vulkan_sdk .. "/Include") or nil,
            shaderc = vulkan_sdk and (vulkan_sdk .. "/Include/shaderc") or nil,
            spirv_cross = vulkan_sdk and (vulkan_sdk .. "/Include/spirv_cross") or nil,
        },
        Libraries = {
            Debug = {
                assimp = baseDir .. "/assimp/bin/Debug/assimp-vc142-mtd.lib",
                reflect_cpp = baseDir .. "/reflect-cpp/bin/Debug/reflectcpp.lib",
                getVulkanLibs("Debug")
            },
            Release = {
                assimp = baseDir .. "/assimp/bin/Release/assimp-vc142-mt.lib",
                reflect_cpp = baseDir .. "/reflect-cpp/bin/Release/reflectcpp.lib",
                getVulkanLibs("Release")
            }
        },
        Defines = {
        }
    },

    TestSpecific = {
        IncludeDir = {
            gtest = baseDir .. "/google-test/include",
            reflect_cpp = baseDir .. "/reflect-cpp/include",
        },
        Libraries = {
            Debug = {
                reflect_cpp = baseDir .. "/reflect-cpp/bin/Debug/reflectcpp.lib",
                gtest = baseDir .. "/google-test/bin/gtestd.lib",
            },
            Release = {
                reflect_cpp = baseDir .. "/reflect-cpp/bin/Release/reflectcpp.lib",
                gtest = baseDir .. "/google-test/bin/gtest.lib",
            }
        },
        Defines = {
            "GTEST",
            "TESTING"  
        },
        EnvVars = {
            GTEST_OUTPUT = "xml:%{wks.location}/build/TestReports/%{prj.name}.xml",
            TEST_REPORTS_DIR = "%{wks.location}/build/TestReports"
        }
    }
}

return Dependencies