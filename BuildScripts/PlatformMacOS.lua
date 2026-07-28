-- macOS-wide workspace settings (Apple Silicon).

filter "system:macosx"
    architecture "ARM64"
    -- 14.0 minimum: Homebrew bottles (shaderc, MoltenVK) target 14.0, and libc++ gates
    -- std::to_chars for floats (std::format) behind 13.3 anyway.
    buildoptions { "-mmacosx-version-min=14.0" }
    linkoptions  { "-mmacosx-version-min=14.0" }

    -- Library search paths for every project: gmake links libraries by name
    -- (-lvulkan, -lassimp, ...), so the Homebrew/LunarG lib dirs must be known.
    if DesertPlatform.HomebrewPrefix then
        libdirs { DesertPlatform.HomebrewPrefix .. "/lib" }
    end
    if os.getenv("VULKAN_SDK") then
        libdirs { os.getenv("VULKAN_SDK") .. "/lib" }
    end

filter {}
