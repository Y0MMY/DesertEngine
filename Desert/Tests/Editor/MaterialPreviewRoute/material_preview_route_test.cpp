// THE PREVIEW MUST NOT FLATTER: a material shown in the Material Preview window has to reach the GPU by
// the same route a mesh in the scene does, so the two are the same picture by construction.
//
// The engine has two routes for a data-driven material, and they are NOT equivalent:
//
//   * the per-SLOT route -- StaticMeshComponent::MaterialSlots -> MaterialFactory -> a DataDrivenMaterial
//     that IS the asset. What a real scene mesh takes.
//   * the shader-OVERRIDE route -- MaterialComponent::ShaderName + per-draw overrides, driving a material
//     shared by shader name. MeshRenderer re-applies ApplyDefaults() to it EVERY frame, so an asset's
//     authored values would be overwritten by schema defaults.
//
// The old 128 px preview took the override route, and that is exactly how it came to show a correct
// material while the scene rendered black two frames in three (Docs/MaterialEditor/STAGE1_END_TO_END.md).
// The fix for the black frames landed in MeshRenderer; this test guards the OTHER half -- that the
// preview keeps asking the same question the game asks.
//
// It fails in BOTH directions on purpose. Someone who moves the preview onto the override route "because
// it is faster" must be stopped and told why, or in six months the flattering preview comes back and
// nobody remembers what it cost to remove.
//
// Why a source-level assertion rather than a behavioural one: PreviewViewport owns a Scene and a
// SceneRenderer, neither of which can be constructed without a Vulkan device, and Scene.cpp is compiled
// by no suite in this repository. Reading the source is the established alternative here --
// Tests/Engine/SettingConsumers does the same thing to Components.hpp, and for the same reason.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
    // The repository root, found by walking up from wherever the test binary was started -- the same
    // approach SettingConsumers and the font-baker test use, so none of them needs one exact directory.
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Editor/Source/Editor/Widgets/PreviewViewport.cpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadFile( const std::string& path )
    {
        std::ifstream in( path );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Source with // line comments stripped, so a comment that merely NAMES the override route (this file's
    // own subject matter is discussed in those headers at length) can never be mistaken for using it.
    std::string CodeOnly( const std::string& source )
    {
        std::string out;
        out.reserve( source.size() );
        size_t i = 0;
        while ( i < source.size() )
        {
            if ( source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/' )
            {
                while ( i < source.size() && source[i] != '\n' )
                    ++i;
            }
            else
            {
                out.push_back( source[i++] );
            }
        }
        return out;
    }

    // The body of one function, from its signature to the matching closing brace.
    //
    // Scoped rather than searching the whole file, and that is not fussiness: the first version of this
    // test looked for "MaterialSlots" anywhere in PreviewViewport.cpp, and a sabotage that tore the slot
    // assignment out of SetMaterial stayed GREEN -- because SetMesh and Clear mention MaterialSlots too.
    // A test that cannot tell which function did the thing is not guarding the function.
    std::string FunctionBody( const std::string& code, const std::string& signature )
    {
        const size_t sig = code.find( signature );
        if ( sig == std::string::npos )
            return {};
        const size_t open = code.find( '{', sig );
        if ( open == std::string::npos )
            return {};

        int    depth = 0;
        size_t i     = open;
        for ( ; i < code.size(); ++i )
        {
            if ( code[i] == '{' )
                ++depth;
            else if ( code[i] == '}' && --depth == 0 )
                break;
        }
        return code.substr( open, i - open );
    }
} // namespace

class MaterialPreviewRoute : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Root = RepoRoot();
        ASSERT_FALSE( m_Root.empty() ) << "repository root not found from the test's working directory";
    }

    std::string Code( const std::string& relative ) const
    {
        const std::string text = ReadFile( m_Root + relative );
        EXPECT_FALSE( text.empty() ) << "could not read " << relative;
        return CodeOnly( text );
    }

    std::string m_Root;
};

// The positive half: SetMaterial itself puts the material in a SLOT.
TEST_F( MaterialPreviewRoute, PreviewViewportPutsTheMaterialInASlot )
{
    const std::string code = Code( "Editor/Source/Editor/Widgets/PreviewViewport.cpp" );
    const std::string body = FunctionBody( code, "PreviewViewport::SetMaterial" );

    ASSERT_FALSE( body.empty() ) << "PreviewViewport::SetMaterial not found — the preview's material entry "
                                    "point is what this suite guards; if it was renamed, re-point the guard "
                                    "rather than deleting it.";
    EXPECT_NE( body.find( "MaterialSlots" ), std::string::npos )
         << "PreviewViewport::SetMaterial no longer assigns StaticMeshComponent::MaterialSlots. The preview "
            "must show a material the way a scene mesh does -- through a material slot -- or it is showing "
            "something the game will not.";
}

// The negative half, and the one that matters in six months: the preview must never reach for the
// override route.
TEST_F( MaterialPreviewRoute, PreviewViewportNeverUsesTheShaderOverrideRoute )
{
    const std::string code = Code( "Editor/Source/Editor/Widgets/PreviewViewport.cpp" );

    EXPECT_EQ( code.find( "MaterialComponent" ), std::string::npos )
         << "PreviewViewport now uses MaterialComponent -- the shader-OVERRIDE route. MeshRenderer calls "
            "ApplyDefaults() on that material every frame, so an asset's authored parameter values are "
            "replaced by schema defaults: the preview would show something the scene does not. This is "
            "precisely what the old 128 px thumbnail did. If a bare shader with no material asset genuinely "
            "needs previewing, give PreviewViewport a separate entry point and leave SetMaterial alone.";
}

// The window must go through the widget's material entry point rather than growing its own path.
//
// RE-POINTED, NOT WEAKENED. The singleton MaterialPreviewPanel became one MaterialEditorPanel per `.demat`
// (Docs/MaterialEditor/PLAN_STAGE3_ASSET_DOCUMENTS.md, M1). The route it must take is unchanged and so is
// every assertion below; only the file that has to take it moved.
TEST_F( MaterialPreviewRoute, TheWindowPreviewsAMaterialAsset )
{
    const std::string code = Code( "Editor/Source/Editor/Panels/MaterialEditor/MaterialEditorPanel.cpp" );

    EXPECT_NE( code.find( "SetMaterial" ), std::string::npos )
         << "MaterialEditorPanel no longer calls PreviewViewport::SetMaterial. A material editor that "
            "previews anything other than the material asset is not previewing what ships.";
    EXPECT_EQ( code.find( "MaterialComponent" ), std::string::npos )
         << "MaterialEditorPanel reaches for MaterialComponent, i.e. the shader-override route. See the "
            "note on PreviewViewportNeverUsesTheShaderOverrideRoute.";
}

// A recompile is only half published: the Shader object reloads itself, but pipelines are cached per
// SceneRenderer and AssetHotReload invalidates only the MAIN scene's cache. A preview that does not drop
// its own would keep drawing the previous compile -- the same disease, new clothes.
TEST_F( MaterialPreviewRoute, ThePreviewDropsItsOwnPipelinesOnARecompile )
{
    const std::string widget = Code( "Editor/Source/Editor/Widgets/PreviewViewport.cpp" );
    EXPECT_NE( widget.find( "InvalidateByShader" ), std::string::npos )
         << "PreviewViewport no longer invalidates its own pipeline cache. AssetHotReload only invalidates "
            "the main scene's, so the preview would keep drawing the shader modules from before the "
            "recompile while the viewport drew the new ones.";

    const std::string graph = Code( "Editor/Source/Editor/Panels/NodeGraph/NodeGraphPanel.cpp" );
    EXPECT_NE( graph.find( "MaterialShaderRebuild::Publish" ), std::string::npos )
         << "Compile no longer tells the Material Editor windows that the shader was rebuilt, so they have "
            "no reason to drop their stale pipelines.";

    // And the window has to LISTEN. The publisher and the listener are asserted together because either one
    // alone is a wire with nothing on the other end -- and one window consuming a shared pending flag is
    // precisely how the several-windows version of this would go wrong silently.
    const std::string window = Code( "Editor/Source/Editor/Panels/MaterialEditor/MaterialEditorPanel.cpp" );
    EXPECT_NE( window.find( "MaterialShaderRebuild::CountFor" ), std::string::npos )
         << "MaterialEditorPanel no longer reads the rebuild count, so a recompile leaves it drawing the "
            "shader modules from before the compile while the viewport draws the ones from after.";
    EXPECT_NE( window.find( "InvalidatePipelines" ), std::string::npos )
         << "MaterialEditorPanel hears about the rebuild and does nothing with it.";
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
