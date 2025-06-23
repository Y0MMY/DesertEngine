local baseDir = "%{wks.location}/ThirdParty"

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
            assimp = baseDir .. "/assimp/include",
            entt = baseDir .. "/entt/include",
            Vulkan = baseDir .. "/VulkanSDK/",
            VulkanInc = baseDir .. "/VulkanSDK/include/",
            shaderc = baseDir .. "/VulkanSDK/shaderc/Include",
            spirv_cross = baseDir .. "/VulkanSDK/spirv_cross/Include",
        },
        Libraries = {
            Debug = {
                assimp = baseDir .. "/assimp/bin/Debug/assimp-vc142-mtd.lib",
                shaderc = baseDir .. "/VulkanSDK/shaderc/Lib/shadercd.lib",
                shaderc_shared = baseDir .. "/VulkanSDK/shaderc/Lib/shaderc_sharedd.lib",
                spirv_cross_core = baseDir .. "/VulkanSDK/spirv_cross/Lib/spirv-cross-cored.lib",
                spirv_cross_glsl = baseDir .. "/VulkanSDK/spirv_cross/Lib/spirv-cross-glsld.lib",
                vulkan = baseDir .. "/VulkanSDK/Lib/vulkan-1.lib",
                OGLCompiler = baseDir .. "/VulkanSDK/Lib/OGLCompilerd.lib",
                shaderc_combined = baseDir .. "/VulkanSDK/shaderc/Lib/shaderc_combinedd.lib",
                shaderc_util = baseDir .. "/VulkanSDK/shaderc/Lib/shaderc_utild.lib",
                reflect_cpp = baseDir .. "/reflect-cpp/bin/Debug/reflectcpp.lib",
            },
            Release = {
                assimp = baseDir .. "/assimp/bin/Release/assimp-vc142-mt.lib",
                shaderc = baseDir .. "/VulkanSDK/shaderc/Lib/shaderc.lib",
                shaderc_shared = baseDir .. "/VulkanSDK/shaderc/Lib/shaderc_shared.lib",
                spirv_cross_core = baseDir .. "/VulkanSDK/spirv_cross/Lib/spirv-cross-core.lib",
                spirv_cross_glsl = baseDir .. "/VulkanSDK/spirv_cross/Lib/spirv-cross-glsl.lib",
                vulkan = baseDir .. "/VulkanSDK/Lib/vulkan-1.lib",
                OGLCompiler = baseDir .. "/VulkanSDK/Lib/OGLCompiler.lib",
                shaderc_combined = baseDir .. "/VulkanSDK/shaderc/Lib/shaderc_combined.lib",
                shaderc_util = baseDir .. "/VulkanSDK/shaderc/Lib/shaderc_util.lib",
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