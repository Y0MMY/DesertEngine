#pragma once

#include "../IPanel.hpp"
#include "ShaderGraph.hpp"

#include <Engine/Assets/Common.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ax::NodeEditor
{
    struct EditorContext;
}

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor
{
    // The SHADER GRAPH editor: an interactive node canvas (imgui-node-editor) over a
    // ShaderGraph::Document that COMPILES to a Desert Shader Language file. Compile writes
    // Resources/Shaders/Programs/Graph/<Name>.shader and registers it with the ShaderService, so
    // the shader immediately appears in the material shader picker; recompiles of an already
    // registered graph go through the normal shader hot-reload. Graphs save/load as .dgraph JSON
    // under Assets/ShaderGraphs/.
    // Hidden by default; enable via View -> Node Graph.
    class NodeGraphPanel final : public IPanel
    {
    public:
        explicit NodeGraphPanel( const std::shared_ptr<Assets::AssetManager>& assetManager );
        ~NodeGraphPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 1100.0f, 680.0f );
        }

        void OnUIRender() override;
        void OnPreUpdate() override;

        // Cross-panel inbox: File Explorer double-click on a .dgraph lands here; the panel opens
        // itself and loads the graph on the next OnPreUpdate (which runs even while hidden).
        static void RequestOpen( const std::string& dgraphPath );

        // Creates a starter .dgraph (unique name) in @p directory and returns its path — the File
        // Explorer's "New Shader Graph" context action; pair with RequestOpen to jump right in.
        static std::string CreateNewGraphFile( const std::string&  directory,
                                               ShaderGraph::Domain domain = ShaderGraph::Domain::Surface );

    private:
        void NewGraph();
        void ChangeDomain( ShaderGraph::Domain domain ); // swaps the output node, prunes off-domain nodes
        void DrawToolbar();
        void DrawCanvas();
        void SaveGraph();
        void LoadGraph( const std::string& fileName );
        void LoadGraphFromPath( const std::string& fullPath );
        void Compile();

        const ShaderGraph::Pin* FindPin( uint64_t id ) const;
        bool                    IsInputPin( uint64_t id ) const;

        // Makes sure a material asset exists that uses this graph's shader, and returns its handle. The
        // preview window is a MATERIAL editor, so a bare shader is not something it can show.
        Assets::AssetHandle EnsurePreviewMaterial();

        // Hand the freshly compiled shader to the Material Preview window: invalidate the pipelines it
        // cached from the old modules, then point it at this graph's material.
        void PublishToPreview();

        std::shared_ptr<Assets::AssetManager> m_AssetManager;
        ax::NodeEditor::EditorContext*        m_Context = nullptr;

        ShaderGraph::Document m_Doc;
        bool                  m_ApplyPositions = true; // push Node.X/Y into the canvas next frame
        std::string           m_Status;                // last save/compile result line
        bool                  m_StatusIsError = false;

        // The material this graph's shader is previewed on, created on the first successful Compile and
        // reused after that (a new asset per compile would litter the project with scratch materials).
        Assets::AssetHandle m_PreviewMaterial{ static_cast<uint64_t>( 0 ) };
    };
} // namespace Desert::Editor
