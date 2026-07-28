#pragma once

#include "../IPanel.hpp"

#include <string>
#include <vector>

namespace ax::NodeEditor
{
    struct EditorContext;
}

namespace Desert::Editor
{
    // Interactive node-graph editor on imgui-node-editor (thedmd, v0.9.3): pan/zoom canvas,
    // draggable nodes, typed pins (float / vector / color), link creation with type checking,
    // node palette on right-click, Del deletes selection, toolbar "Frame" centers the graph.
    // This is the CANVAS layer the future shader-graph (compile to DShader) builds node semantics
    // on — graphs are not yet persisted or compiled.
    // Hidden by default; enable via View -> Node Graph.
    class NodeGraphPanel final : public IPanel
    {
    public:
        NodeGraphPanel();
        ~NodeGraphPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 1100.0f, 680.0f );
        }

        void OnUIRender() override;

        enum class PinType
        {
            Float,
            Vector,
            Color,
        };

        struct Pin
        {
            uint64_t    Id;
            std::string Name;
            PinType     Type;
        };

        struct Node
        {
            uint64_t         Id;
            std::string      Title;
            ImU32            HeaderColor;
            std::vector<Pin> Inputs;
            std::vector<Pin> Outputs;
        };

        struct Link
        {
            uint64_t Id;
            uint64_t From; // output pin id
            uint64_t To;   // input pin id
        };

    private:
        Node&      AddNode( const char* title, ImU32 headerColor, std::vector<Pin>&& inputs,
                            std::vector<Pin>&& outputs );
        Pin        MakePin( const char* name, PinType type );
        const Pin* FindPin( uint64_t id ) const;
        bool       IsInputPin( uint64_t id ) const;

        ax::NodeEditor::EditorContext* m_Context = nullptr;

        std::vector<Node> m_Nodes;
        std::vector<Link> m_Links;
        uint64_t          m_NextId    = 1;
        bool              m_FirstOpen = true;
    };
} // namespace Desert::Editor
