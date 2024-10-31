#pragma once

#include <filesystem>

namespace Common::Constants // TODO: should be merge from config
{
    namespace Path
    {
        const std::filesystem::path RESOURCE_PATH         = "Resources/";
        const std::filesystem::path RESOURCE_SPIRV_BINARY = "Resources/Shaders/SPIRV/Bin/";
        const std::filesystem::path SCENE_PATH            = "Resources/Assets/Scene/";
    } // namespace Path

    namespace Extensions
    {
        const std::string SPIRV_BINARY_EXTENSION_VERT = ".spvbin_vert";
        const std::string SPIRV_BINARY_EXTENSION_FRAG = ".spvbin_frag";
        const std::string SPIRV_BINARY_EXTENSION_COMP = ".spvbin_comp";
    } // namespace Extensions
} // namespace Common::Constants