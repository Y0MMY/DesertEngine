// One scene, one lighting payload — asserted as a RELATION between the skinned path and the static one.
//
// The defect this suite was written for: SkinnedMaterialPBR::Bind() named the four things a skinned mesh
// was thought to need (camera, lights, bones, cloud shadow) and the scene had six. The two it did not
// name — the shadow cascades and the IBL environment — were written by the static path only, so a
// skinned mesh was the one class of geometry in the engine that received neither. Nothing crashed and no
// validation layer said anything, because an unwritten descriptor here is not undefined memory: the
// backend seeds every binding first (VulkanMaterialBackend::InitializeWithFallbacks), so `ShadowUB` kept
// the zero-filled dummy buffer — `u_ShadowParams.y == 0`, cascades silently OFF — while the environment
// trio kept its fallback images, which sample black, so the split-sum ambient was zero and a skinned
// surface was lit by the sun and the anti-black floor alone no matter what the sky was doing.
//
// Two wrong answers, both silent, both invisible to a unit test of either side. So the assertions here
// are about the two sides AGREEING:
//
//   * the skinned material's Bind() payload IS the static path's snapshot type, and has no way of being
//     built without one (a reference member has no default);
//   * every binding that ONE applier fills is declared by ALL three mesh PBR shaders, by NAME — which is
//     what a material actually looks up;
//   * the three shaders' set 0 differ only in the per-object buffer their vertex stage reads, so "one
//     applier serves all of them" is a fact and not an intention;
//   * the `ShadowUB` block is the same number of bytes in GLSL as the C++ struct the applier fills.
//
// None of it needs a device: the shaders are compiled with shaderc and reflected with the engine's own
// reflection, exactly as Tests/Engine/ShaderCacheKey does.

#include <gtest/gtest.h>

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShaderReflection.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/MaterialPBRBase.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/PBRSceneFrame.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/SkinnedMaterialPBR.hpp>
#include <Engine/Graphic/ShaderProtocols/Camera.hpp>
#include <Engine/Graphic/ShaderProtocols/DirectionLight.hpp>
#include <Engine/Graphic/ShaderProtocols/Metadata.hpp>
#include <Engine/Graphic/ShaderProtocols/PointLight.hpp>
#include <Engine/Graphic/ShaderProtocols/SkinnedMaterialUB.hpp>
#include <Engine/Graphic/ShaderProtocols/SpotLight.hpp>

#include <Common/Core/Constants.hpp>

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using Desert::Core::Formats::ShaderStage;
using Desert::Graphic::MaterialPBRBase;
using Desert::Graphic::PBRSceneFrame;
using Desert::Graphic::SkinnedMaterialPBR;
using namespace Desert::Graphic::API::Vulkan;

namespace
{
    // The engine resolves `#include <...>` against Common::Constants::Path::SHADERDIR_PATH, which is
    // relative ("Resources/Shaders/"). The editor runs with its own directory as the working one; the
    // test does the same so the include walk resolves the same files the runtime would.
    struct ShaderRootFixture : ::testing::Test
    {
        static void SetUpTestSuite()
        {
            std::filesystem::path here = std::filesystem::current_path();
            for ( int up = 0; up < 8 && !std::filesystem::exists( here / "Editor" / "Resources" / "Shaders" );
                  ++up )
                here = here.parent_path();

            ASSERT_TRUE( std::filesystem::exists( here / "Editor" / "Resources" / "Shaders" ) )
                 << "could not find Editor/Resources/Shaders above " << std::filesystem::current_path();

            std::filesystem::current_path( here / "Editor" );
        }
    };

    std::string ReadFile( const std::filesystem::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    std::filesystem::path ShaderPath( const char* relative )
    {
        return std::filesystem::path( "Resources/Shaders/Programs" ) / relative;
    }

    // The assembled GLSL of one stage, straight out of the engine's own DSL parser.
    std::string StageSource( const std::filesystem::path& shaderFile, ShaderStage stage )
    {
        auto parsed = Desert::Core::Preprocess::DShaderParser::Parse( ReadFile( shaderFile ) );
        EXPECT_TRUE( parsed.IsSuccess() ) << shaderFile.string();
        if ( !parsed.IsSuccess() )
            return {};
        const auto it = parsed.GetValue().Stages.find( stage );
        EXPECT_NE( it, parsed.GetValue().Stages.end() ) << shaderFile.string();
        return it == parsed.GetValue().Stages.end() ? std::string{} : it->second;
    }

    // Resolves `#include <...>` exactly as ShaderIncluder does, so the SPIR-V under test is the SPIR-V
    // the engine compiles.
    class Includer final : public shaderc::CompileOptions::IncluderInterface
    {
    public:
        shaderc_include_result* GetInclude( const char* requested, shaderc_include_type type,
                                            const char* requesting, size_t ) override
        {
            const std::filesystem::path full =
                 type == shaderc_include_type_relative
                      ? ( std::filesystem::path( requesting ).parent_path() / requested ).lexically_normal()
                      : ( Common::Constants::Path::SHADERDIR_PATH / requested ).lexically_normal();

            auto* name = new std::string( full.string() );
            auto* body =
                 new std::string( Desert::Core::Preprocess::DShaderParser::TranslateSugar( ReadFile( full ) ) );

            auto* result               = new shaderc_include_result;
            result->source_name        = name->c_str();
            result->source_name_length = name->size();
            result->content            = body->c_str();
            result->content_length     = body->size();
            result->user_data          = new std::pair<std::string*, std::string*>( name, body );
            return result;
        }

        void ReleaseInclude( shaderc_include_result* data ) override
        {
            auto* pair = static_cast<std::pair<std::string*, std::string*>*>( data->user_data );
            delete pair->first;
            delete pair->second;
            delete pair;
            delete data;
        }
    };

    std::vector<uint32_t> CompileStage( const std::string& source, const std::filesystem::path& path,
                                        shaderc_shader_kind kind )
    {
        shaderc::Compiler       compiler;
        shaderc::CompileOptions options;
        options.SetIncluder( std::make_unique<Includer>() );
        options.SetTargetEnvironment( shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1 );

        const auto result = compiler.CompileGlslToSpv( source, kind, path.string().c_str(), options );
        EXPECT_EQ( result.GetCompilationStatus(), shaderc_compilation_status_success )
             << path.string() << ": " << result.GetErrorMessage();
        if ( result.GetCompilationStatus() != shaderc_compilation_status_success )
            return {};
        return { result.begin(), result.end() };
    }

    // Set 0 of a graphics shader, both stages folded together — which is the set a material allocates and
    // therefore the set an applier writes into.
    ShaderResource::ShaderDescriptorSet GraphicsSetZero( const std::filesystem::path& shaderFile )
    {
        const auto vertexSpirv =
             CompileStage( StageSource( shaderFile, ShaderStage::Vertex ), shaderFile, shaderc_vertex_shader );
        const auto fragmentSpirv =
             CompileStage( StageSource( shaderFile, ShaderStage::Fragment ), shaderFile, shaderc_fragment_shader );
        if ( vertexSpirv.empty() || fragmentSpirv.empty() )
            return {};

        ShaderResource::ReflectionData data;
        auto diagnostics = ShaderReflection::ReflectStage( vertexSpirv, ShaderStage::Vertex, data );
        EXPECT_TRUE( diagnostics.empty() ) << ( diagnostics.empty() ? "" : diagnostics.front() );
        diagnostics = ShaderReflection::ReflectStage( fragmentSpirv, ShaderStage::Fragment, data );
        EXPECT_TRUE( diagnostics.empty() ) << ( diagnostics.empty() ? "" : diagnostics.front() );

        const auto it = data.ShaderDescriptorSets.find( 0 );
        return it == data.ShaderDescriptorSets.end() ? ShaderResource::ShaderDescriptorSet{} : it->second;
    }

    // The NAMES set 0 declares. Names and not slots on purpose: Material::Get looks a property up by
    // name, so a name is what the CPU and the shader actually have to agree on.
    std::set<std::string> DeclaredNames( const ShaderResource::ShaderDescriptorSet& set )
    {
        std::set<std::string> names;
        for ( const auto& [binding, resource] : set.UniformBuffers )
            names.insert( resource.Name );
        for ( const auto& [binding, resource] : set.StorageBuffers )
            names.insert( resource.Name );
        for ( const auto& [binding, resource] : set.Image2DSamplers )
            names.insert( resource.Name );
        for ( const auto& [binding, resource] : set.ImageCubeSamplers )
            names.insert( resource.Name );
        for ( const auto& [binding, resource] : set.Image3DSamplers )
            names.insert( resource.Name );
        return names;
    }

    std::string Describe( const std::set<std::string>& names )
    {
        std::ostringstream out;
        for ( const auto& name : names )
            out << name << ' ';
        return out.str();
    }

    // Every resource PBRSceneFrame::ApplyTo fills, by the same name the applier looks it up under. The
    // camera and the light blocks come from the ShaderProtocols types that own those names; the shadow
    // and environment names come from MaterialPBRBase, which is the writer. Nothing here is a literal
    // repeated from the engine — a rename that reaches only one side fails to compile, not to pass.
    //
    // The cloud-shadow pair (u_CloudShadowMap / CloudShadowUB) is deliberately NOT here: ApplyTo writes it
    // through Graphic::CloudShadowBind, and Tests/Engine/CloudShadow already asserts that every sun-lit
    // shader in the tree — these three included — declares it.
    std::vector<std::string> SceneBindingNames()
    {
        std::vector<std::string> names = {
             Desert::Graphic::ShaderProtocols::Camera::Name,
             Desert::Graphic::ShaderProtocols::PointLight::Name,
             Desert::Graphic::ShaderProtocols::SpotLight::Name,
             Desert::Graphic::ShaderProtocols::DirectionLight::Name,
             Desert::Graphic::ShaderProtocols::LightsMetadata::Name,
             MaterialPBRBase::kShadowBlockName,
             MaterialPBRBase::kEnvIrradianceName,
             MaterialPBRBase::kEnvSpecularName,
             MaterialPBRBase::kBrdfLutName,
        };
        for ( uint32_t c = 0; c < MaterialPBRBase::kMaxCascades; ++c )
            names.emplace_back( MaterialPBRBase::kShadowMapNames[c] );
        return names;
    }

    // The three shaders one applier has to serve, each with the ONE resource that is genuinely its own —
    // the per-object buffer its vertex stage reads. Everything else in their set 0 is scene state.
    struct MeshShader
    {
        const char* Path;
        const char* PerObjectVertexBuffer; // nullptr = none
    };

    const MeshShader kMeshShaders[] = {
         { "PBR/StaticMeshPBR.shader", nullptr },
         { "PBR/StaticMeshPBR_Instanced.shader", "InstanceTransforms" },
         { "PBR/SkinnedMeshPBR.shader", "Bones" },
    };
} // namespace

// ---- The payload ------------------------------------------------------------------------------------

// THE relation, in the only form in which it can be stated at compile time: the skinned material's
// lighting payload is not a payload of its own that happens to carry the same fields — it IS the snapshot
// the static path applies, and there is no way to bind a skinned mesh without one.
TEST( PBRSceneFrame, TheSkinnedMaterialCannotBeBoundWithoutTheSnapshotTheStaticPathApplies )
{
    using Info = SkinnedMaterialPBR::UpdateSkinnedMaterialPBRInfo;

    static_assert( std::is_same_v<decltype( Info::Scene ), const PBRSceneFrame&>,
                   "the skinned Bind() payload must carry THE scene snapshot, not a copy of the parts of "
                   "it someone thought a skinned mesh needs — that copy is exactly how the cascades and "
                   "the environment cubes went missing" );

    // A reference member has no default, so the aggregate cannot be built without a snapshot. This is the
    // defect made impossible rather than merely fixed: a future field added to PBRSceneFrame reaches the
    // skinned path by construction.
    static_assert( !std::is_default_constructible_v<Info>,
                   "a skinned draw must not be constructible without the frame's scene state" );

    EXPECT_FALSE( std::is_default_constructible_v<Info> );
}

// ---- The applier against the shaders ----------------------------------------------------------------

TEST_F( ShaderRootFixture, EverySceneBindingTheOneApplierFillsIsDeclaredByEveryMeshPBRShader )
{
    const auto expected = SceneBindingNames();
    ASSERT_FALSE( expected.empty() );

    for ( const auto& shader : kMeshShaders )
    {
        const auto declared = DeclaredNames( GraphicsSetZero( ShaderPath( shader.Path ) ) );
        ASSERT_FALSE( declared.empty() ) << shader.Path;

        for ( const auto& name : expected )
            EXPECT_TRUE( declared.count( name ) != 0 )
                 << shader.Path << " does not declare '" << name
                 << "', which PBRSceneFrame::ApplyTo fills for it. Declared: " << Describe( declared );
    }
}

// The other direction, and the one that would have caught Д15 the day it was written: a scene-level
// resource DECLARED by a mesh shader that no applier fills. Before this task SkinnedMeshPBR declared
// ShadowUB, four cascade maps and the environment trio, and the skinned draw path wrote none of them —
// the shader sampled the fallbacks and read as unshadowed and blown-out white.
TEST_F( ShaderRootFixture, NoMeshPBRShaderDeclaresASceneResourceNoApplierFills )
{
    // Everything in a mesh PBR set 0 that is genuinely per-OBJECT and is therefore filled by the material
    // itself rather than by the frame snapshot: the GPU-scene material row and the surface maps
    // (MaterialFactory binds these three by name from the material asset).
    const char* kPerObject[] = { "Materials", "u_AlbedoTexture", "u_NormalTexture", "u_OpacityTexture" };
    // The cloud-shadow pair, filled by Graphic::CloudShadowBind out of the same snapshot — see the note on
    // SceneBindingNames().
    const char* kCloudShadow[] = { "u_CloudShadowMap", "CloudShadowUB" };

    for ( const auto& shader : kMeshShaders )
    {
        const auto declared = DeclaredNames( GraphicsSetZero( ShaderPath( shader.Path ) ) );
        ASSERT_FALSE( declared.empty() ) << shader.Path;

        std::set<std::string> accounted;
        for ( const auto& name : SceneBindingNames() )
            accounted.insert( name );
        for ( const char* name : kPerObject )
            accounted.insert( name );
        for ( const char* name : kCloudShadow )
            accounted.insert( name );
        if ( shader.PerObjectVertexBuffer )
            accounted.insert( shader.PerObjectVertexBuffer );

        for ( const auto& name : declared )
            EXPECT_TRUE( accounted.count( name ) != 0 )
                 << shader.Path << " declares '" << name
                 << "' and nothing in the engine writes it — an unwritten binding is not a disabled "
                    "feature, it is whatever the fallback descriptor happens to contain";
    }
}

// One scene contract, three shaders. They differ ONLY in the per-object buffer their vertex stage reads,
// which is what makes a single applier able to serve all three — and what makes a divergence a failure
// here rather than a class of geometry lit differently from the rest of the scene.
TEST_F( ShaderRootFixture, TheThreeMeshPBRShadersDeclareOneSceneContractAndDifferOnlyInTheirOwnBuffer )
{
    std::set<std::string> reference;

    for ( const auto& shader : kMeshShaders )
    {
        std::set<std::string> declared = DeclaredNames( GraphicsSetZero( ShaderPath( shader.Path ) ) );
        ASSERT_FALSE( declared.empty() ) << shader.Path;

        if ( shader.PerObjectVertexBuffer )
        {
            EXPECT_TRUE( declared.erase( shader.PerObjectVertexBuffer ) != 0 )
                 << shader.Path << " no longer declares its own " << shader.PerObjectVertexBuffer;
        }

        if ( reference.empty() )
            reference = declared;
        else
            EXPECT_EQ( Describe( declared ), Describe( reference ) )
                 << shader.Path << " no longer shares set 0 with StaticMeshPBR, but one applier writes both";
    }
}

// ---- The block the applier fills --------------------------------------------------------------------

// The C++ mirror against the GLSL block: two statements of one layout, which is the disagreement shape
// this project has paid for repeatedly. Asserted on all three shaders, because the mirror is filled once
// and lands in three different descriptor sets.
TEST_F( ShaderRootFixture, TheShadowBlockIsTheSameBytesInTheApplierAndInEveryMeshPBRShader )
{
    // 4 x mat4 + 3 x vec4. Spelt out so a silently added member is visible as a number here.
    constexpr uint32_t kExpectedBytes = MaterialPBRBase::kMaxCascades * 64u + 3u * 16u;
    EXPECT_EQ( sizeof( MaterialPBRBase::ShadowUBData ), kExpectedBytes );

    for ( const auto& shader : kMeshShaders )
    {
        const auto set = GraphicsSetZero( ShaderPath( shader.Path ) );

        const auto block =
             std::find_if( set.UniformBuffers.begin(), set.UniformBuffers.end(), []( const auto& entry )
                           { return entry.second.Name == MaterialPBRBase::kShadowBlockName; } );

        ASSERT_NE( block, set.UniformBuffers.end() ) << shader.Path << " declares no ShadowUB";
        EXPECT_EQ( block->second.Size, sizeof( MaterialPBRBase::ShadowUBData ) )
             << shader.Path << "'s ShadowUB is " << block->second.Size << " bytes and the struct the "
             << "applier fills it from is " << sizeof( MaterialPBRBase::ShadowUBData );
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
