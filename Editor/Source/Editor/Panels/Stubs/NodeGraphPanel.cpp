#include "NodeGraphPanel.hpp"

#include <imgui-node-editor/imgui_node_editor.h>

#include <algorithm>

namespace ed = ax::NodeEditor;

namespace Desert::Editor
{
    namespace
    {
        ImU32 PinColor( NodeGraphPanel::PinType type )
        {
            switch ( type )
            {
                case NodeGraphPanel::PinType::Float:  return IM_COL32( 145, 210, 130, 255 );
                case NodeGraphPanel::PinType::Vector: return IM_COL32( 240, 200, 90, 255 );
                case NodeGraphPanel::PinType::Color:  return IM_COL32( 235, 120, 120, 255 );
            }
            return IM_COL32_WHITE;
        }
    } // namespace

    NodeGraphPanel::NodeGraphPanel() : IPanel( "Node Graph", /*showPanel=*/false )
    {
        ed::Config config;
        config.SettingsFile = nullptr; // don't scatter .json layout files next to the binary
        m_Context           = ed::CreateEditor( &config );

        // The node refs below live across AddNode calls — keep the vector from reallocating.
        m_Nodes.reserve( 64 );

        // Demo "surface shader" sketch, now fully interactive: rewire/delete it, add nodes from the
        // right-click palette. The future shader graph replaces these with real DShader semantics.
        auto& texture = AddNode( "Texture Sample", IM_COL32( 70, 110, 160, 255 ), { MakePin( "UV", PinType::Vector ) },
                                 { MakePin( "RGBA", PinType::Color ), MakePin( "R", PinType::Float ) } );
        auto& scalar  = AddNode( "Scalar (0.5)", IM_COL32( 80, 130, 90, 255 ), {},
                                 { MakePin( "Value", PinType::Float ) } );
        auto& mul     = AddNode( "Multiply", IM_COL32( 90, 90, 120, 255 ),
                                 { MakePin( "A", PinType::Color ), MakePin( "B", PinType::Float ) },
                                 { MakePin( "Out", PinType::Color ) } );
        auto& output  = AddNode( "Surface Output", IM_COL32( 150, 90, 60, 255 ),
                                 { MakePin( "Albedo", PinType::Color ), MakePin( "Roughness", PinType::Float ),
                                   MakePin( "Emission", PinType::Color ) },
                                 {} );

        m_Links.push_back( { m_NextId++, texture.Outputs[0].Id, mul.Inputs[0].Id } ); // RGBA -> A
        m_Links.push_back( { m_NextId++, scalar.Outputs[0].Id, mul.Inputs[1].Id } );  // Value -> B
        m_Links.push_back( { m_NextId++, mul.Outputs[0].Id, output.Inputs[0].Id } );  // Out -> Albedo
        m_Links.push_back( { m_NextId++, texture.Outputs[1].Id, output.Inputs[1].Id } ); // R -> Roughness
    }

    NodeGraphPanel::~NodeGraphPanel()
    {
        if ( m_Context )
            ed::DestroyEditor( m_Context );
    }

    NodeGraphPanel::Pin NodeGraphPanel::MakePin( const char* name, PinType type )
    {
        return Pin{ m_NextId++, name, type };
    }

    NodeGraphPanel::Node& NodeGraphPanel::AddNode( const char* title, ImU32 headerColor,
                                                   std::vector<Pin>&& inputs, std::vector<Pin>&& outputs )
    {
        Node node;
        node.Id          = m_NextId++;
        node.Title       = title;
        node.HeaderColor = headerColor;
        node.Inputs      = std::move( inputs );
        node.Outputs     = std::move( outputs );
        m_Nodes.push_back( std::move( node ) );
        return m_Nodes.back();
    }

    const NodeGraphPanel::Pin* NodeGraphPanel::FindPin( uint64_t id ) const
    {
        for ( const auto& node : m_Nodes )
        {
            for ( const auto& pin : node.Inputs )
                if ( pin.Id == id )
                    return &pin;
            for ( const auto& pin : node.Outputs )
                if ( pin.Id == id )
                    return &pin;
        }
        return nullptr;
    }

    bool NodeGraphPanel::IsInputPin( uint64_t id ) const
    {
        for ( const auto& node : m_Nodes )
            for ( const auto& pin : node.Inputs )
                if ( pin.Id == id )
                    return true;
        return false;
    }

    void NodeGraphPanel::OnUIRender()
    {
        // Slim toolbar. Everything else is the canvas.
        if ( ImGui::Button( "Frame" ) )
        {
            ed::SetCurrentEditor( m_Context );
            ed::NavigateToContent( 0.4f );
            ed::SetCurrentEditor( nullptr );
        }
        ImGui::SameLine();
        ImGui::TextDisabled( "Right-click: add node  |  Del: delete  |  drag pins to connect" );

        ed::SetCurrentEditor( m_Context );
        ed::Begin( "##nodeGraph", ImVec2( 0.0f, 0.0f ) );

        // --- Nodes ---
        for ( auto& node : m_Nodes )
        {
            ed::BeginNode( ed::NodeId( node.Id ) );

            ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( node.HeaderColor ), "%s",
                                node.Title.c_str() );
            ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );

            ImGui::BeginGroup(); // inputs
            for ( const auto& pin : node.Inputs )
            {
                ed::BeginPin( ed::PinId( pin.Id ), ed::PinKind::Input );
                ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( PinColor( pin.Type ) ), "-> %s",
                                    pin.Name.c_str() );
                ed::EndPin();
            }
            if ( node.Inputs.empty() )
                ImGui::Dummy( ImVec2( 1.0f, 1.0f ) );
            ImGui::EndGroup();

            ImGui::SameLine( 0.0f, 48.0f );

            ImGui::BeginGroup(); // outputs
            for ( const auto& pin : node.Outputs )
            {
                ed::BeginPin( ed::PinId( pin.Id ), ed::PinKind::Output );
                ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( PinColor( pin.Type ) ), "%s ->",
                                    pin.Name.c_str() );
                ed::EndPin();
            }
            if ( node.Outputs.empty() )
                ImGui::Dummy( ImVec2( 1.0f, 1.0f ) );
            ImGui::EndGroup();

            ed::EndNode();
        }

        // --- Links (coloured by the SOURCE pin's type) ---
        for ( const auto& link : m_Links )
        {
            const Pin* from  = FindPin( link.From );
            const ImU32 col  = from ? PinColor( from->Type ) : IM_COL32( 200, 200, 200, 255 );
            ed::Link( ed::LinkId( link.Id ), ed::PinId( link.From ), ed::PinId( link.To ),
                      ImGui::ColorConvertU32ToFloat4( col ), 2.0f );
        }

        // --- Interactive link creation with type checking ---
        if ( ed::BeginCreate() )
        {
            ed::PinId a, b;
            if ( ed::QueryNewLink( &a, &b ) && a && b )
            {
                uint64_t from = a.Get(), to = b.Get();
                if ( IsInputPin( from ) )
                    std::swap( from, to ); // normalize: from = output, to = input

                const Pin* fromPin = FindPin( from );
                const Pin* toPin   = FindPin( to );

                const bool kindsOk = fromPin && toPin && !IsInputPin( from ) && IsInputPin( to );
                const bool typesOk = kindsOk && fromPin->Type == toPin->Type;
                const bool freeOk  = typesOk && std::none_of( m_Links.begin(), m_Links.end(),
                                                              [&]( const Link& l ) { return l.To == to; } );

                if ( !kindsOk )
                    ed::RejectNewItem( ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ), 2.0f ); // out->out / in->in
                else if ( !typesOk || !freeOk )
                    ed::RejectNewItem( ImVec4( 1.0f, 0.5f, 0.2f, 1.0f ), 2.0f ); // type mismatch / occupied
                else if ( ed::AcceptNewItem( ImVec4( 0.5f, 1.0f, 0.5f, 1.0f ), 3.0f ) )
                    m_Links.push_back( { m_NextId++, from, to } );
            }
        }
        ed::EndCreate();

        // --- Deletion (Del key / context) ---
        if ( ed::BeginDelete() )
        {
            ed::LinkId deletedLink;
            while ( ed::QueryDeletedLink( &deletedLink ) )
            {
                if ( ed::AcceptDeletedItem() )
                    std::erase_if( m_Links, [&]( const Link& l ) { return l.Id == deletedLink.Get(); } );
            }

            ed::NodeId deletedNode;
            while ( ed::QueryDeletedNode( &deletedNode ) )
            {
                if ( ed::AcceptDeletedItem() )
                {
                    const uint64_t id = deletedNode.Get();
                    if ( auto it = std::find_if( m_Nodes.begin(), m_Nodes.end(),
                                                 [&]( const Node& n ) { return n.Id == id; } );
                         it != m_Nodes.end() )
                    {
                        // Links attached to any of the node's pins die with it.
                        std::erase_if( m_Links,
                                       [&]( const Link& l )
                                       {
                                           auto owns = [&]( uint64_t pin )
                                           {
                                               for ( const auto& p : it->Inputs )
                                                   if ( p.Id == pin )
                                                       return true;
                                               for ( const auto& p : it->Outputs )
                                                   if ( p.Id == pin )
                                                       return true;
                                               return false;
                                           };
                                           return owns( l.From ) || owns( l.To );
                                       } );
                        m_Nodes.erase( it );
                    }
                }
            }
        }
        ed::EndDelete();

        // --- Right-click palette on the canvas background ---
        const ImVec2 popupCanvasPos = ImGui::GetMousePos(); // canvas coords while inside Begin/End
        ed::Suspend();
        if ( ed::ShowBackgroundContextMenu() )
            ImGui::OpenPopup( "##nodePalette" );

        if ( ImGui::BeginPopup( "##nodePalette" ) )
        {
            ImGui::TextDisabled( "Add node" );
            ImGui::Separator();

            Node* created = nullptr;
            if ( ImGui::MenuItem( "Texture Sample" ) )
                created = &AddNode( "Texture Sample", IM_COL32( 70, 110, 160, 255 ),
                                    { MakePin( "UV", PinType::Vector ) },
                                    { MakePin( "RGBA", PinType::Color ), MakePin( "R", PinType::Float ) } );
            if ( ImGui::MenuItem( "Scalar" ) )
                created = &AddNode( "Scalar", IM_COL32( 80, 130, 90, 255 ), {},
                                    { MakePin( "Value", PinType::Float ) } );
            if ( ImGui::MenuItem( "Multiply" ) )
                created = &AddNode( "Multiply", IM_COL32( 90, 90, 120, 255 ),
                                    { MakePin( "A", PinType::Color ), MakePin( "B", PinType::Float ) },
                                    { MakePin( "Out", PinType::Color ) } );
            if ( ImGui::MenuItem( "Add" ) )
                created = &AddNode( "Add", IM_COL32( 90, 90, 120, 255 ),
                                    { MakePin( "A", PinType::Color ), MakePin( "B", PinType::Color ) },
                                    { MakePin( "Out", PinType::Color ) } );
            if ( ImGui::MenuItem( "Lerp" ) )
                created = &AddNode( "Lerp", IM_COL32( 120, 90, 130, 255 ),
                                    { MakePin( "A", PinType::Color ), MakePin( "B", PinType::Color ),
                                      MakePin( "T", PinType::Float ) },
                                    { MakePin( "Out", PinType::Color ) } );
            if ( ImGui::MenuItem( "Surface Output" ) )
                created = &AddNode( "Surface Output", IM_COL32( 150, 90, 60, 255 ),
                                    { MakePin( "Albedo", PinType::Color ),
                                      MakePin( "Roughness", PinType::Float ),
                                      MakePin( "Emission", PinType::Color ) },
                                    {} );

            if ( created )
                ed::SetNodePosition( ed::NodeId( created->Id ), popupCanvasPos );

            ImGui::EndPopup();
        }
        ed::Resume();

        ed::End();

        // First open: lay out the demo nodes left-to-right, then frame them.
        if ( m_FirstOpen )
        {
            m_FirstOpen  = false;
            const ImVec2 positions[] = { { 0.0f, 40.0f }, { 30.0f, 240.0f }, { 280.0f, 60.0f }, { 520.0f, 90.0f } };
            for ( size_t i = 0; i < m_Nodes.size() && i < std::size( positions ); ++i )
                ed::SetNodePosition( ed::NodeId( m_Nodes[i].Id ), positions[i] );
            ed::NavigateToContent( 0.0f );
        }

        ed::SetCurrentEditor( nullptr );
    }
} // namespace Desert::Editor
