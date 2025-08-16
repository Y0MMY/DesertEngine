# ThirdParty/CMakeLists.txt
# Remove these lines:
# add_subdirectory(stb)
# add_subdirectory(VulkanSDK)
# add_subdirectory(VulkanSDK/shaderc)
# add_subdirectory(VulkanSDK/spirv_cross)

# Replace with:
# STB (single-header library)
add_library(stb STATIC 
    ${CMAKE_SOURCE_DIR}/ThirdParty/stb/stb_image.cpp
)
target_include_directories(stb PUBLIC 
    ${CMAKE_SOURCE_DIR}/ThirdParty/stb/include
)

# Vulkan - use the system installation
find_package(Vulkan REQUIRED)

# For shaderc and spirv-cross, use the installed versions if available
find_package(shaderc REQUIRED)
find_package(SPIRV-Cross REQUIRED)