#pragma once

#include "../IPanel.hpp"

#include <string>
#include <vector>

namespace Desert::Editor
{
    // VISUAL STUB (no real functionality yet): a generic node-graph canvas — pan/zoom grid, draggable
    // demo nodes with typed pins, bezier links. The future shader-graph / visual-scripting editors will
    // grow out of this canvas; for now it exists so the interaction + look can be iterated on early.
    // Hidden by default; enable via View -> Node Graph.
    class NodeGraphPanel final : public IPanel
    {
    public:
        NodeGraphPanel();
        void OnUIRender() override;

    private:
        struct Pin
        {
            std::string Name;
            ImU32       Color;
        };
        struct Node
        {
            std::string      Title;
            ImVec2           Pos;  // canvas space
            ImVec2           Size; // computed on draw
            std::vector<Pin> Inputs;
            std::vector<Pin> Outputs;
            ImU32            HeaderColor;
        };
        struct Link
        {
            int FromNode, FromPin; // output pin of FromNode
            int ToNode, ToPin;     // input pin of ToNode
        };

        ImVec2 PinPos( const Node& node, int pin, bool output ) const; // canvas space

        std::vector<Node> m_Nodes;
        std::vector<Link> m_Links;

        ImVec2 m_Scroll{ 60.0f, 40.0f };
        float  m_Zoom         = 1.0f;
        int    m_DraggingNode = -1;
        int    m_SelectedNode = -1;
    };
} // namespace Desert::Editor
