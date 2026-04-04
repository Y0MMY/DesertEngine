local baseDir = "%{wks.location}/ThirdParty"

Dependencies = {


    EditorSpecific = {
         IncludeDir = {
            imGuizmo   = "ThirdParty/ImGuizmo",
            assimp     = "ThirdParty/assimp/include",
            reflect_cpp = baseDir .. "/reflect-cpp/include",
        },

        Libraries = {
            Debug = {
                assimp = "ThirdParty/assimp/lib/Debug/assimp-vc142-mtd.lib",
            },

            Release = {
                assimp =  "ThirdParty/assimp/lib/Release/assimp-vc142-mt.lib",
            }
        }
    }
}

return Dependencies