// The two things that decided whether a shader edit reaches the GPU — tested without one.
//
// Both are here because a real failure needed both to be understood. A compute shader gained a binding
// while the editor was running; the validation layer then reported, every frame:
//
//     VkDescriptorSetLayout from VkPipelineLayout has 8 total descriptors,
//     but VkDescriptorSetLayout, trying to bind, has 9 total descriptors
//
// Two candidates: a SPIR-V cache key that missed the edit (so one compile saw the old shader), or a
// descriptor set layout destroyed under a live pipeline (so two compiles disagreed). Only the second
// turned out to be true — and the only way to say that here, with no Vulkan, is to be able to compute
// both numbers on the CPU:
//
//   1. THE CACHE KEY — Core::ComputeShaderCacheKey and Core::CollectShaderIncludes, the functions the
//      compiler itself calls. A key that does not move when an included .glslh moves is the worst kind
//      of failure there is: the machine with the stale artifact renders differently from the machine
//      without it, and neither says anything.
//
//   2. THE DESCRIPTOR COUNT — ShaderReflection::BuildLayoutBindings over the REAL shaders, which is the
//      shape VulkanShader turns into a VkDescriptorSetLayout. The number this produces is the number
//      the validation layer compares, so a shader that gains a binding is visible here first.

#include <gtest/gtest.h>

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>
#include <Engine/Core/ShaderCompiler/ShaderCacheKey.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShaderReflection.hpp>
#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>

#include <Common/Core/Constants.hpp>

#include <shaderc/shaderc.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Desert::Core::CollectShaderIncludes;
using Desert::Core::ComputeShaderCacheKey;
using Desert::Core::Formats::ShaderStage;
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
            // The test binary lives in build/Bin/Tests/<config>; the shader root is Editor/Resources.
            std::filesystem::path here = std::filesystem::current_path();
            for ( int up = 0; up < 8 && !std::filesystem::exists( here / "Editor" / "Resources" / "Shaders" );
                  ++up )
                here = here.parent_path();

            s_RepoRoot = here;
            ASSERT_TRUE( std::filesystem::exists( s_RepoRoot / "Editor" / "Resources" / "Shaders" ) )
                 << "could not find Editor/Resources/Shaders above " << std::filesystem::current_path();

            std::filesystem::current_path( s_RepoRoot / "Editor" );
        }

        static std::filesystem::path s_RepoRoot;
    };

    std::filesystem::path ShaderRootFixture::s_RepoRoot;

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

    // The assembled GLSL of one stage, straight out of the engine's own DSL parser — the same string
    // the compiler hashes and hands to shaderc.
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
        options.SetWarningsAsErrors();

        const auto result = compiler.CompileGlslToSpv( source, kind, path.string().c_str(), options );
        EXPECT_EQ( result.GetCompilationStatus(), shaderc_compilation_status_success )
             << path.string() << ": " << result.GetErrorMessage();
        if ( result.GetCompilationStatus() != shaderc_compilation_status_success )
            return {};
        return { result.begin(), result.end() };
    }

    // Reflects one compute shader and returns the layout bindings of set 0 — the contract a pipeline
    // layout and a descriptor set have to agree on.
    std::vector<VkDescriptorSetLayoutBinding> ComputeSetZero( const std::filesystem::path& shaderFile )
    {
        const std::string source = StageSource( shaderFile, ShaderStage::Compute );
        const auto        spirv  = CompileStage( source, shaderFile, shaderc_compute_shader );
        if ( spirv.empty() )
            return {};

        ShaderResource::ReflectionData data;
        const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Compute, data );
        EXPECT_TRUE( diagnostics.empty() ) << ( diagnostics.empty() ? "" : diagnostics.front() );

        const auto it = data.ShaderDescriptorSets.find( 0 );
        return it == data.ShaderDescriptorSets.end() ? std::vector<VkDescriptorSetLayoutBinding>{}
                                                     : ShaderReflection::BuildLayoutBindings( it->second );
    }

    // The same reflection for a FRAGMENT stage. A separate function rather than a stage parameter on the
    // one above, because the two shaderc kinds are the only difference and a bool argument at the call
    // site reads as nothing at all.
    std::vector<VkDescriptorSetLayoutBinding> FragmentSetZero( const std::filesystem::path& shaderFile )
    {
        const std::string source = StageSource( shaderFile, ShaderStage::Fragment );
        const auto        spirv  = CompileStage( source, shaderFile, shaderc_fragment_shader );
        if ( spirv.empty() )
            return {};

        ShaderResource::ReflectionData data;
        const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Fragment, data );
        EXPECT_TRUE( diagnostics.empty() ) << ( diagnostics.empty() ? "" : diagnostics.front() );

        const auto it = data.ShaderDescriptorSets.find( 0 );
        return it == data.ShaderDescriptorSets.end() ? std::vector<VkDescriptorSetLayoutBinding>{}
                                                     : ShaderReflection::BuildLayoutBindings( it->second );
    }

    bool HasBinding( const std::vector<VkDescriptorSetLayoutBinding>& bindings, uint32_t binding,
                     VkDescriptorType type )
    {
        for ( const auto& b : bindings )
            if ( b.binding == binding )
                return b.descriptorType == type && b.descriptorCount == 1;
        return false;
    }

    // A temporary .glslh next to the real ones, so an include can be edited without touching the tree.
    struct ScopedHeader
    {
        explicit ScopedHeader( std::string body )
             : Path( std::filesystem::path( "Resources/Shaders/Common" ) / "CacheKeyTestScratch.glslh" )
        {
            Write( std::move( body ) );
        }

        ~ScopedHeader()
        {
            std::error_code ec;
            std::filesystem::remove( Path, ec );
        }

        void Write( std::string body ) const
        {
            std::ofstream out( Path, std::ios::binary | std::ios::trunc );
            out << body;
        }

        std::filesystem::path Path;
    };
} // namespace

// ---- The cache key ----------------------------------------------------------------------------------

TEST_F( ShaderRootFixture, TheKeyIsStableForUnchangedInput )
{
    const std::string source = "#version 450\nvoid main() {}\n";
    const auto        path   = ShaderPath( "Fog/HeightFog.shader" );

    EXPECT_EQ( ComputeShaderCacheKey( ShaderStage::Compute, source, path ),
               ComputeShaderCacheKey( ShaderStage::Compute, source, path ) );
}

TEST_F( ShaderRootFixture, EditingTheStageSourceMovesTheKey )
{
    const auto path = ShaderPath( "Fog/HeightFog.shader" );

    EXPECT_NE( ComputeShaderCacheKey( ShaderStage::Compute, "#version 450\nvoid main() {}\n", path ),
               ComputeShaderCacheKey( ShaderStage::Compute, "#version 450\nvoid main() { }\n", path ) );
}

TEST_F( ShaderRootFixture, TheStageIsPartOfTheKey )
{
    const std::string source = "#version 450\nvoid main() {}\n";
    const auto        path   = ShaderPath( "Fog/HeightFog.shader" );

    // Vertex and fragment SPIR-V from identical text are different binaries, and a key that ignored the
    // stage would serve one for the other.
    EXPECT_NE( ComputeShaderCacheKey( ShaderStage::Vertex, source, path ),
               ComputeShaderCacheKey( ShaderStage::Fragment, source, path ) );
}

TEST_F( ShaderRootFixture, EditingAnIncludedHeaderMovesTheKey )
{
    // THE property the cache rests on. The stage source does not change at all here — only a file it
    // includes does, which is exactly the case a naive key gets wrong.
    ScopedHeader header( "// v1\nconst float kScratch = 1.0f;\n" );

    const std::string source = "#version 450\n#include <Common/CacheKeyTestScratch.glslh>\nvoid main() {}\n";
    const auto        path   = ShaderPath( "Fog/HeightFog.shader" );

    const uint64_t before = ComputeShaderCacheKey( ShaderStage::Compute, source, path );

    header.Write( "// v2\nconst float kScratch = 2.0f;\n" );
    const uint64_t after = ComputeShaderCacheKey( ShaderStage::Compute, source, path );

    EXPECT_NE( before, after );

    // ...and it comes back when the header does: the key is a function of content, not of edit count.
    header.Write( "// v1\nconst float kScratch = 1.0f;\n" );
    EXPECT_EQ( ComputeShaderCacheKey( ShaderStage::Compute, source, path ), before );
}

TEST_F( ShaderRootFixture, AnIncludeThatDoesNotResolveIsNotFatal )
{
    const std::string source = "#version 450\n#include <Common/NoSuchHeaderAnywhere.glslh>\nvoid main() {}\n";
    const auto        path   = ShaderPath( "Fog/HeightFog.shader" );

    EXPECT_TRUE( CollectShaderIncludes( source, path ).empty() );
    EXPECT_NO_THROW( (void)ComputeShaderCacheKey( ShaderStage::Compute, source, path ) );
}

// ---- The include closure ----------------------------------------------------------------------------

TEST_F( ShaderRootFixture, TheClosureOfTheFogPassListsEveryHeaderItNames )
{
    const auto path     = ShaderPath( "Fog/HeightFog.shader" );
    const auto source   = StageSource( path, ShaderStage::Compute );
    const auto includes = CollectShaderIncludes( source, path );

    const auto contains = [&includes]( const char* name )
    {
        for ( const auto& include : includes )
            if ( include.filename() == name )
                return true;
        return false;
    };

    EXPECT_TRUE( contains( "HeightFog.glslh" ) );
    EXPECT_TRUE( contains( "FogParams.glslh" ) );
    EXPECT_TRUE( contains( "SkyMedium.glslh" ) );
    EXPECT_TRUE( contains( "SkyScattering.glslh" ) );
}

TEST_F( ShaderRootFixture, TheClosureFollowsAHeaderThatIncludesAnother )
{
    // The TRANSITIVE step, which is what makes the walk worth having over a single grep of the stage
    // source: NewShaderGraph names Common/GraphVertex.glslh, and only GraphVertex names
    // Common/CameraUB.glslh. A key that stopped at depth one would not move when CameraUB was edited,
    // and the machine holding the stale SPIR-V would render differently from the one that had none.
    const auto path     = ShaderPath( "Graph/NewShaderGraph.shader" );
    const auto includes = CollectShaderIncludes( StageSource( path, ShaderStage::Vertex ), path );

    const auto contains = [&includes]( const char* name )
    {
        for ( const auto& include : includes )
            if ( include.filename() == name )
                return true;
        return false;
    };

    EXPECT_TRUE( contains( "GraphVertex.glslh" ) );
    EXPECT_TRUE( contains( "CameraUB.glslh" ) );
}

TEST_F( ShaderRootFixture, TheClosureListsEachFileOnce )
{
    const auto path     = ShaderPath( "Fog/HeightFog.shader" );
    const auto includes = CollectShaderIncludes( StageSource( path, ShaderStage::Compute ), path );

    std::vector<std::string> seen;
    for ( const auto& include : includes )
    {
        const std::string key = include.generic_string();
        EXPECT_EQ( std::find( seen.begin(), seen.end(), key ), seen.end() ) << "duplicate: " << key;
        seen.push_back( key );
    }
    EXPECT_FALSE( seen.empty() );
}

// ---- The descriptor count a pipeline layout and a bound set have to agree on ------------------------

TEST_F( ShaderRootFixture, TheFogEvaluationDeclaresFiveDescriptorsInSetZero )
{
    // The regression guard for the failure this test file exists for: a pipeline built before a shader
    // gained a binding and a set allocated after it cannot be bound together. Five is not a magic number
    // — it is counted from the shader's own text by the engine's own reflection, so it moves when the
    // shader does.
    const auto bindings = ComputeSetZero( ShaderPath( "Fog/HeightFog.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 5u );

    EXPECT_TRUE( HasBinding( bindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ) );          // the fog it writes
    EXPECT_TRUE( HasBinding( bindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );         // fog params
    EXPECT_TRUE( HasBinding( bindings, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // scene depth
    EXPECT_TRUE( HasBinding( bindings, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // AP volume
    EXPECT_TRUE( HasBinding( bindings, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // distant sky light
}

TEST_F( ShaderRootFixture, TheDistantSkyLightDeclaresFourDescriptorsInSetZero )
{
    // A second pass through the same reflection, and the one that would notice the sky's LUT chain
    // gaining or losing an input: the fill reads the cached transmittance and multi-scatter pair and
    // writes exactly one image.
    const auto bindings = ComputeSetZero( ShaderPath( "Sky/SkyDistantLight.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 4u );

    EXPECT_TRUE( HasBinding( bindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ) );          // the texel it fills
    EXPECT_TRUE( HasBinding( bindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );         // sky params
    EXPECT_TRUE( HasBinding( bindings, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // transmittance LUT
    EXPECT_TRUE( HasBinding( bindings, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // multi-scatter LUT
}

TEST_F( ShaderRootFixture, TheCloudShadowMapDeclaresFourDescriptorsInSetZero )
{
    // THE PRODUCER OF THE CLOUD SHADOW MAP, and the reason it is pinned here rather than trusted: its
    // three inputs are bound by NUMBER and not by reflection (ComputePipeline::SetInput takes the binding
    // as an argument), so the C++ constants in Engine/Graphic/Clouds/CloudShadowPayload.hpp and the
    // numbers in the shader are two statements of one fact. The assertions below are that fact, checked
    // against the compiled SPIR-V rather than against the file's text.
    //
    // The three inputs are DELIBERATELY the march's own slot numbers — one vocabulary for one field —
    // which is why 3 and 7 are occupied and 1, 2, 4, 5, 6 are not.
    const auto bindings = ComputeSetZero( ShaderPath( "Clouds/CloudShadowMap.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 4u );

    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowOutputBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ) ); // the triple it writes
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowParamsBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) ); // the cloud parameter block
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowNoiseBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the noise volume
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowProfileBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the vertical profile table
}

TEST_F( ShaderRootFixture, TheDeferredLightingPassDeclaresSeventeenDescriptorsInSetZero )
{
    // THE CONSUMER, and the pass this repository shares most widely — every deferred scene draws it, and
    // it is the one file the cloud work was told to touch as little as possible. Pinning its descriptor
    // set is how "as little as possible" becomes checkable: the count moves the day somebody adds a
    // binding to it, whether or not they meant to.
    //
    // Seventeen, and they are every slot from 0 to 16 with none free in between: six G-buffer and scene
    // samplers (1, 2, 3, 8, 9, 10), four cascade maps (5, 13, 14, 15), four uniform blocks (0, 4, 7, 12),
    // two light SSBOs (6, 16) and the cloud shadow map (11). Two of those — 11 and 12 — are this task's,
    // and they are the only two it added.
    const auto bindings = FragmentSetZero( ShaderPath( "Deferred/DeferredLighting.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 17u );

    EXPECT_TRUE( HasBinding( bindings, 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_CloudShadowMap
    EXPECT_TRUE( HasBinding( bindings, 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );         // CloudShadowUB

    // And the cascade block it must NOT have disturbed: the same four maps at the same four slots, with
    // ShadowUB where PBR.glsl.frag mirrors it.
    EXPECT_TRUE( HasBinding( bindings, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 14, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 15, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );
}

TEST_F( ShaderRootFixture, TheBindingsComeOutSortedAndCountedTheWayTheLayerCounts )
{
    // The layer reports "N total descriptors", which is the SUM of descriptorCount, not the number of
    // bindings. They agree here only because every binding this engine builds has a count of one — the
    // day that stops being true, this is where it is noticed.
    const auto bindings = ComputeSetZero( ShaderPath( "Fog/HeightFog.shader" ) );

    ASSERT_FALSE( bindings.empty() );
    for ( size_t i = 1; i < bindings.size(); ++i )
        EXPECT_LT( bindings[i - 1].binding, bindings[i].binding );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), bindings.size() );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
