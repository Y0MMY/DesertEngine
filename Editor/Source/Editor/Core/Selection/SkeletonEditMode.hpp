#pragma once

namespace Desert::Editor::Core
{
    // Blender-style "Skeleton Edit" mode toggle. When active, the viewport draws the selected skinned mesh's
    // skeleton (bone heads + parent->child links) as an overlay, and the bone tree drives a selected-bone
    // highlight. Object/Scene mode = inactive. Phase 1 is view-only; gizmo editing comes later.
    class SkeletonEditMode final
    {
    public:
        static bool IsActive()
        {
            return s_Active;
        }
        static void SetActive( bool active )
        {
            s_Active = active;
            if ( !active )
            {
                s_SelectedBone = -1;
                s_PoseMode     = false; // leaving skeleton edit also leaves pose-authoring
            }
        }
        static void Toggle()
        {
            SetActive( !s_Active );
        }

        // When true, the bone gizmo edits the ANIMATED pose (the Animator's editable local-pose buffer) rather
        // than the rig's bind/rest pose — used by the Sequencer to author clips by posing. False (default) =
        // rig/rest-pose editing. The Sequencer drives this while it is authoring a clip.
        static bool PoseMode()
        {
            return s_PoseMode;
        }
        static void SetPoseMode( bool on )
        {
            s_PoseMode = on;
        }

        // Index into Skeleton::GetBones() of the highlighted bone, or -1 for none.
        static int GetSelectedBone()
        {
            return s_SelectedBone;
        }
        static void SetSelectedBone( int bone )
        {
            s_SelectedBone = bone;
        }

        // When false (default) only the selected bone is labelled in the viewport (dense rigs overlap all
        // names into an unreadable blob otherwise); true labels every bone.
        static bool ShowAllNames()
        {
            return s_ShowAllNames;
        }
        static void SetShowAllNames( bool show )
        {
            s_ShowAllNames = show;
        }

    private:
        static inline bool s_Active       = false;
        static inline int  s_SelectedBone = -1;
        static inline bool s_ShowAllNames = false;
        static inline bool s_PoseMode     = false;
    };
} // namespace Desert::Editor::Core
