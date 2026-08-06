#pragma once

#include <string>
#include <unordered_set>

namespace Desert::Editor::Core
{
    // "Open that panel" as an EXPLICIT request, from anywhere, by panel name.
    //
    // Tool panels used to open themselves off the selection (IPanel::IsContextual): click an animated
    // character and the Anim Graph, the Layers panel and the Sequencer all threw themselves over the
    // editor. Selecting an object is not a request to author its animation — so those panels are opened
    // by a BUTTON now (Details ▸ Animation ▸ "Anim Graph", …), and this is the wire between the button
    // and the panel: the requester does not need the panel's type, its instance, or its header.
    //
    // The editor's panel loop consumes each request once per frame and shows + focuses that panel.
    class PanelRequests
    {
    public:
        // Name as registered with the panel (IPanel::GetName()).
        static void Open( const std::string& panelName )
        {
            Pending().insert( panelName );
        }

        // True once for a name that was requested; clears it.
        static bool ConsumeOpen( const std::string& panelName )
        {
            auto&      pending = Pending();
            const auto it      = pending.find( panelName );
            if ( it == pending.end() )
                return false;
            pending.erase( it );
            return true;
        }

    private:
        static std::unordered_set<std::string>& Pending()
        {
            static std::unordered_set<std::string> s_Pending;
            return s_Pending;
        }
    };
} // namespace Desert::Editor::Core
