#pragma once

#include <Engine/Assets/Common.hpp>

#include <optional>
#include <vector>

namespace Desert::Editor::Core
{
    // "Open THIS asset in whatever edits it" — the wire between a panel that received an asset (the browser's
    // double-click, the Node Graph's Compile) and EditorLayer, which is the only place that may create or
    // destroy a document window.
    //
    // WHY THIS IS NOT [[PanelRequests]]. PanelRequests carries a panel NAME and no payload, so it can say
    // "show the Material Preview" and never "show MP_GreenTint". That is exactly why the editor grew three
    // hand-wired file-static inboxes instead (NodeGraphPanel::RequestOpen, MaterialPreviewPanel::
    // RequestPreview, SceneOpenRequest) — each one a private wire for one asset kind. This is the one wire
    // that carries the subject, so the next asset kind adds a factory rather than a fourth inbox.
    struct AssetOpenRequest
    {
        Assets::AssetHandle Subject;
        Assets::AssetTypeID Type = Assets::AssetTypeID::Unknown;
    };

    // The pending requests, drained once per frame by EditorLayer.
    //
    // A QUEUE, not the single slot [[SceneOpenRequest]] uses, and the difference is not style: only one scene
    // can be THE document, so a second scene request in a frame genuinely replaces the first — whereas two
    // materials can both be open, so dropping one would lose a window the user asked for. Requests for the
    // same subject in one frame collapse to one, because open-or-focus would make the second a no-op anyway
    // and two log lines about it would only read like a defect.
    class AssetOpenRequests
    {
    public:
        static void Request( const Assets::AssetHandle& subject, Assets::AssetTypeID type )
        {
            auto& pending = Pending();
            for ( const auto& queued : pending )
            {
                if ( queued.Subject == subject && queued.Type == type )
                    return;
            }
            pending.push_back( AssetOpenRequest{ subject, type } );
        }

        // Everything queued since the last drain; clears the queue.
        static std::vector<AssetOpenRequest> Drain()
        {
            std::vector<AssetOpenRequest> drained;
            drained.swap( Pending() );
            return drained;
        }

    private:
        static std::vector<AssetOpenRequest>& Pending()
        {
            static std::vector<AssetOpenRequest> s_Pending;
            return s_Pending;
        }
    };
} // namespace Desert::Editor::Core
