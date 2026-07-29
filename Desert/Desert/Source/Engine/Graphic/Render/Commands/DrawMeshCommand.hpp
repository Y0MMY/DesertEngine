#pragma once

#include "../RenderCommand.hpp"
#include <Engine/Geometry/Mesh.hpp>
#include <glm/mat4x4.hpp>

namespace Desert::Graphic::Render
{
    struct DrawStaticMeshCommand : RenderCommand
    {
        Desert::Mesh* Mesh;
        // Pointer to the component's stable RuntimeSlotPtrs (valid for the frame, never mutated between the
        // ECS update that records this command and its same-frame execution) — avoids copying a slot vector
        // per entity through the submission chain (Debug-heavy).
        const std::vector<Graphic::MaterialInstance*>* MaterialSlots;
        glm::mat4                                      Transform;
        bool                                           Outlined        = false;
        uint64_t                                       HiddenSubmeshes = 0;  // bit i = submesh i hidden
        int                                            ForcedLOD       = -1; // -1 = auto (by distance)
        int                                            LODBias         = 0;  // shifts the auto LOD (ignored when forced)
        bool                                           CastShadows     = true;
        bool                                           ReceiveShadows  = true;

        DrawStaticMeshCommand( Desert::Mesh* mesh, const std::vector<Graphic::MaterialInstance*>* materialSlots,
                               const glm::mat4& transform, bool outlined = false, uint64_t hiddenSubmeshes = 0,
                               int forcedLOD = -1, int lodBias = 0, bool castShadows = true,
                               bool receiveShadows = true )
             : Mesh( mesh ), MaterialSlots( materialSlots ), Transform( transform ), Outlined( outlined ),
               HiddenSubmeshes( hiddenSubmeshes ), ForcedLOD( forcedLOD ), LODBias( lodBias ),
               CastShadows( castShadows ), ReceiveShadows( receiveShadows )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            if ( MaterialSlots )
                renderer.SubmitMesh( Mesh, *MaterialSlots, Transform,
                                     { .Outlined        = Outlined,
                                       .HiddenSubmeshes = HiddenSubmeshes,
                                       .ForcedLOD       = ForcedLOD,
                                       .LODBias         = LODBias,
                                       .CastShadows     = CastShadows,
                                       .ReceiveShadows  = ReceiveShadows } );
        }
    };

    // UE-style Instanced Static Mesh: one mesh + one material drawn for every transform. Transforms point
    // at the component's stable per-frame array (valid until the command buffer is cleared between frames).
    struct DrawInstancedStaticMeshCommand : RenderCommand
    {
        Desert::Mesh*                 Mesh;
        Graphic::MaterialInstance*    Material;
        const std::vector<glm::mat4>* Transforms;

        DrawInstancedStaticMeshCommand( Desert::Mesh* mesh, Graphic::MaterialInstance* material,
                                        const std::vector<glm::mat4>* transforms )
             : Mesh( mesh ), Material( material ), Transforms( transforms )
        {
        }

        void Execute( SceneRenderer& renderer ) override
        {
            if ( Mesh && Material && Transforms )
                renderer.SubmitInstancedMesh( Mesh, Material, Transforms );
        }
    };
} // namespace Desert::Graphic::Render