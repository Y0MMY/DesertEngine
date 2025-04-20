-- Config file for Radiant Engine

workspace "Desert"
    configurations { "Debug", "Release" }
    architecture "x64"
    startproject "Sandbox"

    language "C++"
	cppdialect "C++20"
    targetdir "build/Bin/%{cfg.buildcfg}"
	objdir "build/Intermediates/%{cfg.buildcfg}"

	externalanglebrackets "On"
	externalwarnings "Off"
	warnings "Off"

filter "configurations:Debug"
    runtime "Debug"
    symbols "On"

filter "configurations:Release"
    runtime "Release"
    optimize "On"



group "ThirdParty"
include "ThirdParty/"
group ""

include "Desert/"
include "Sandbox/"

