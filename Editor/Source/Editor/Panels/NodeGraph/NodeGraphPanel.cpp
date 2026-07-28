#include "NodeGraphPanel.hpp"

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Shader/ShaderAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <imgui-node-editor/imgui_node_editor.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace ed = ax::NodeEditor;

namespace Desert::Editor
{
    // Inside Desert::* the unqualified name ImGui resolves to the engine's Desert::ImGui wrapper —
    // alias the real Dear ImGui back in (same trick the other panels use).
    namespace ImGui = ::ImGui;

    namespace
    {
        namespace SG = ShaderGraph;

        ImU32 PinColor( int type )
        {
            switch ( static_cast<SG::ValueType>( type ) )
            {
                case SG::ValueType::Float: return IM_COL32( 145, 210, 130, 255 );
                case SG::ValueType::Vec2:  return IM_COL32( 240, 200, 90, 255 );
                case SG::ValueType::Color: return IM_COL32( 235, 120, 120, 255 );
            }
            return IM_COL32_WHITE;
        }

        std::filesystem::path GraphsDirectory()
        {
            return Common::Constants::Path::ASSETS_PATH / "ShaderGraphs";
        }

        std::filesystem::path CompiledShaderPath( const std::string& name )
        {
            return Common::Constants::Path::SHADERDIR_PATH / "Programs" / "Graph" / ( name + ".shader" );
        }
    } // namespace

    NodeGraphPanel::NodeGraphPanel( const std::shared_ptr<Assets::AssetManager>& assetManager )
         : IPanel( "Node Graph", /*showPanel=*/false ), m_AssetManager( assetManager )
    {
        ed::Config config;
        config.SettingsFile = nullptr; // node positions live in the .dgraph, not a stray json
        m_Context           = ed::CreateEditor( &config );

        NewGraph();
    }

    NodeGraphPanel::~NodeGraphPanel()
    {
        if ( m_Context )
            ed::DestroyEditor( m_Context );
    }

    void NodeGraphPanel::NewGraph()
    {
        m_Doc = {};

        // Starter graph: BaseColor param -> Surface Output — compiles as-is.
        auto param = SG::MakeNode( m_Doc, "ColorParam" );
        param.ParamName = "BaseColor";
        param.Value     = { 0.8f, 0.4f, 0.1f, 1.0f };
        param.X = 0.0f;
        param.Y = 60.0f;

        auto output = SG::MakeNode( m_Doc, "SurfaceOutput" );
        output.X = 320.0f;
        output.Y = 40.0f;

        m_Doc.Links.push_back( { m_Doc.NextId++, param.Outputs[0].Id, output.Inputs[0].Id } );
        m_Doc.Nodes.push_back( std::move( param ) );
        m_Doc.Nodes.push_back( std::move( output ) );

        m_ApplyPositions = true;
        m_Status.clear();
    }

    const SG::Pin* NodeGraphPanel::FindPin( uint64_t id ) const
    {
        for ( const auto& node : m_Doc.Nodes )
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
        for ( const auto& node : m_Doc.Nodes )
            for ( const auto& pin : node.Inputs )
                if ( pin.Id == id )
                    return true;
        return false;
    }

    void NodeGraphPanel::SaveGraph()
    {
        // Node positions are canvas state — pull them into the document before writing.
        ed::SetCurrentEditor( m_Context );
        for ( auto& node : m_Doc.Nodes )
        {
            const ImVec2 pos = ed::GetNodePosition( ed::NodeId( node.Id ) );
            node.X           = pos.x;
            node.Y           = pos.y;
        }
        ed::SetCurrentEditor( nullptr );

        std::error_code ec;
        std::filesystem::create_directories( GraphsDirectory(), ec );
        const auto path = GraphsDirectory() / ( m_Doc.Name + ".dgraph" );
        Common::Utils::FileSystem::WriteContentToFile( path, SG::Serialize( m_Doc ) );
        m_Status        = "Saved " + path.filename().string();
        m_StatusIsError = false;
    }

    void NodeGraphPanel::LoadGraph( const std::string& fileName )
    {
        const auto path = GraphsDirectory() / fileName;
        if ( !std::filesystem::exists( path ) )
            return;

        auto parsed = SG::Deserialize( Common::Utils::FileSystem::ReadFileContent( path ) );
        if ( !parsed )
        {
            m_Status        = parsed.GetError();
            m_StatusIsError = true;
            return;
        }
        m_Doc            = parsed.GetValue();
        m_ApplyPositions = true;
        m_Status         = "Loaded " + fileName;
        m_StatusIsError  = false;
    }

    void NodeGraphPanel::Compile()
    {
        const auto source = SG::CompileToDShader( m_Doc );
        if ( !source )
        {
            m_Status        = "Compile error: " + source.GetError();
            m_StatusIsError = true;
            return;
        }

        const auto path = CompiledShaderPath( m_Doc.Name );
        std::error_code ec;
        std::filesystem::create_directories( path.parent_path(), ec );
        Common::Utils::FileSystem::WriteContentToFile( path, source.GetValue() );

        const auto shaderService = Runtime::ResourceRegistry::GetShaderService();
        if ( shaderService->GetByName( m_Doc.Name ) )
        {
            // Already registered: the overwrite is picked up by the shader hot-reload poll.
            m_Status        = m_Doc.Name + ".shader recompiled — hot reload applies it";
            m_StatusIsError = false;
            return;
        }

        // First compile: register the new shader so it shows up in the material picker right away.
        const auto asset =
             m_AssetManager->CreateAsset<Assets::ShaderAsset>( Assets::AssetPriority::Medium, path );
        if ( !asset || !asset->IsReadyForUse() )
        {
            m_Status        = "wrote " + path.string() + " but shader failed to load (see log)";
            m_StatusIsError = true;
            return;
        }
        if ( const auto registered = shaderService->Register( asset ); !registered )
        {
            m_Status        = registered.GetError();
            m_StatusIsError = true;
            return;
        }
        m_Status        = m_Doc.Name + " compiled + registered — pick it in Material \\ Shader";
        m_StatusIsError = false;
    }

    void NodeGraphPanel::DrawToolbar()
    {
        char nameBuf[64];
        std::snprintf( nameBuf, sizeof( nameBuf ), "%s", m_Doc.Name.c_str() );
        ImGui::SetNextItemWidth( 160.0f );
        if ( ImGui::InputText( "##graphName", nameBuf, sizeof( nameBuf ) ) )
            m_Doc.Name = nameBuf;

        ImGui::SameLine();
        if ( ImGui::Button( "New" ) )
            NewGraph();
        ImGui::SameLine();
        if ( ImGui::Button( "Save" ) )
            SaveGraph();

        ImGui::SameLine();
        if ( ImGui::Button( "Load" ) )
            ImGui::OpenPopup( "##loadGraph" );
        if ( ImGui::BeginPopup( "##loadGraph" ) )
        {
            bool any = false;
            std::error_code ec;
            for ( const auto& entry : std::filesystem::directory_iterator( GraphsDirectory(), ec ) )
            {
                if ( entry.path().extension() != ".dgraph" )
                    continue;
                any = true;
                if ( ImGui::MenuItem( entry.path().filename().string().c_str() ) )
                    LoadGraph( entry.path().filename().string() );
            }
            if ( !any )
                ImGui::TextDisabled( "no saved graphs" );
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        if ( ImGui::Button( "Compile" ) )
            Compile();

        ImGui::SameLine();
        if ( ImGui::Button( "Frame" ) )
        {
            ed::SetCurrentEditor( m_Context );
            ed::NavigateToContent( 0.4f );
            ed::SetCurrentEditor( nullptr );
        }

        if ( !m_Status.empty() )
        {
            ImGui::SameLine();
            ImGui::TextColored( m_StatusIsError ? ImVec4( 1.0f, 0.45f, 0.4f, 1.0f )
                                                : ImVec4( 0.5f, 0.9f, 0.5f, 1.0f ),
                                "%s", m_Status.c_str() );
        }
    }

    void NodeGraphPanel::DrawCanvas()
    {
        ed::SetCurrentEditor( m_Context );
        ed::Begin( "##shaderGraph", ImVec2( 0.0f, 0.0f ) );

        // --- Nodes ---
        for ( auto& node : m_Doc.Nodes )
        {
            const SG::NodeSpec* spec = SG::FindSpec( node.Kind );

            if ( m_ApplyPositions )
                ed::SetNodePosition( ed::NodeId( node.Id ), ImVec2( node.X, node.Y ) );

            ed::BeginNode( ed::NodeId( node.Id ) );

            const ImU32 header = spec ? spec->HeaderColor : IM_COL32_WHITE;
            ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( header ), "%s",
                                spec ? spec->Title : node.Kind.c_str() );
            ImGui::Dummy( ImVec2( 0.0f, 2.0f ) );

            // Editable payload (param name / value) — inline, no popups (canvas-safe).
            if ( spec && spec->HasParamName )
            {
                char buf[48];
                std::snprintf( buf, sizeof( buf ), "%s", node.ParamName.c_str() );
                ImGui::SetNextItemWidth( 140.0f );
                if ( ImGui::InputText( ( "##pn" + std::to_string( node.Id ) ).c_str(), buf,
                                       sizeof( buf ) ) )
                    node.ParamName = buf;
            }
            if ( spec && spec->HasColorValue )
            {
                ImGui::SetNextItemWidth( 200.0f );
                ImGui::DragFloat4( ( "##cv" + std::to_string( node.Id ) ).c_str(), node.Value.data(),
                                   0.01f, 0.0f, 4.0f, "%.2f" );
            }
            if ( spec && spec->HasFloatValue )
            {
                ImGui::SetNextItemWidth( 100.0f );
                ImGui::DragFloat( ( "##fv" + std::to_string( node.Id ) ).c_str(), node.Value.data(),
                                  0.01f, -64.0f, 64.0f, "%.2f" );
            }

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

            ImGui::SameLine( 0.0f, 40.0f );

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
        if ( m_ApplyPositions )
            ed::NavigateToContent( 0.0f ); // fresh/new/loaded graph: frame it
        m_ApplyPositions = false;

        // --- Links (coloured by the source pin's type) ---
        for ( const auto& link : m_Doc.Links )
        {
            const SG::Pin* from = FindPin( link.From );
            const ImU32    col  = from ? PinColor( from->Type ) : IM_COL32( 200, 200, 200, 255 );
            ed::Link( ed::LinkId( link.Id ), ed::PinId( link.From ), ed::PinId( link.To ),
                      ImGui::ColorConvertU32ToFloat4( col ), 2.0f );
        }

        // --- Link creation with type checking ---
        if ( ed::BeginCreate() )
        {
            ed::PinId a, b;
            if ( ed::QueryNewLink( &a, &b ) && a && b )
            {
                uint64_t from = a.Get(), to = b.Get();
                if ( IsInputPin( from ) )
                    std::swap( from, to );

                const SG::Pin* fromPin = FindPin( from );
                const SG::Pin* toPin   = FindPin( to );

                const bool kindsOk = fromPin && toPin && !IsInputPin( from ) && IsInputPin( to );
                const bool typesOk = kindsOk && fromPin->Type == toPin->Type;
                const bool freeOk =
                     typesOk && std::none_of( m_Doc.Links.begin(), m_Doc.Links.end(),
                                              [&]( const SG::Link& l ) { return l.To == to; } );

                if ( !kindsOk )
                    ed::RejectNewItem( ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ), 2.0f );
                else if ( !typesOk || !freeOk )
                    ed::RejectNewItem( ImVec4( 1.0f, 0.5f, 0.2f, 1.0f ), 2.0f );
                else if ( ed::AcceptNewItem( ImVec4( 0.5f, 1.0f, 0.5f, 1.0f ), 3.0f ) )
                    m_Doc.Links.push_back( { m_Doc.NextId++, from, to } );
            }
        }
        ed::EndCreate();

        // --- Deletion ---
        if ( ed::BeginDelete() )
        {
            ed::LinkId deletedLink;
            while ( ed::QueryDeletedLink( &deletedLink ) )
            {
                if ( ed::AcceptDeletedItem() )
                    std::erase_if( m_Doc.Links,
                                   [&]( const SG::Link& l ) { return l.Id == deletedLink.Get(); } );
            }

            ed::NodeId deletedNode;
            while ( ed::QueryDeletedNode( &deletedNode ) )
            {
                if ( ed::AcceptDeletedItem() )
                {
                    const uint64_t id = deletedNode.Get();
                    if ( auto it = std::find_if( m_Doc.Nodes.begin(), m_Doc.Nodes.end(),
                                                 [&]( const SG::Node& n ) { return n.Id == id; } );
                         it != m_Doc.Nodes.end() )
                    {
                        std::erase_if( m_Doc.Links,
                                       [&]( const SG::Link& l )
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
                        m_Doc.Nodes.erase( it );
                    }
                }
            }
        }
        ed::EndDelete();

        // --- Right-click palette ---
        const ImVec2 popupCanvasPos = ImGui::GetMousePos();
        ed::Suspend();
        if ( ed::ShowBackgroundContextMenu() )
            ImGui::OpenPopup( "##nodePalette" );

        if ( ImGui::BeginPopup( "##nodePalette" ) )
        {
            ImGui::TextDisabled( "Add node" );
            ImGui::Separator();

            const bool hasOutput =
                 std::any_of( m_Doc.Nodes.begin(), m_Doc.Nodes.end(),
                              []( const SG::Node& n ) { return n.Kind == "SurfaceOutput"; } );

            for ( const auto& spec : SG::Specs() )
            {
                if ( spec.Kind == std::string( "SurfaceOutput" ) && hasOutput )
                    continue; // exactly one output per graph
                if ( ImGui::MenuItem( spec.Title ) )
                {
                    auto node = SG::MakeNode( m_Doc, spec.Kind );
                    node.X    = popupCanvasPos.x;
                    node.Y    = popupCanvasPos.y;
                    ed::SetNodePosition( ed::NodeId( node.Id ), popupCanvasPos );
                    m_Doc.Nodes.push_back( std::move( node ) );
                }
            }
            ImGui::EndPopup();
        }
        ed::Resume();

        ed::End();
        ed::SetCurrentEditor( nullptr );
    }

    void NodeGraphPanel::OnUIRender()
    {
        DrawToolbar();
        DrawCanvas();
    }
} // namespace Desert::Editor
