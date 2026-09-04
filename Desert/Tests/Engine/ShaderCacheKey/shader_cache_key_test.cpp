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
#include <Engine/Graphic/Clouds/CloudAuthoredPayload.hpp>
#include <Engine/Graphic/Clouds/CloudEnvironmentBake.hpp>
#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>
#include <Engine/Graphic/Clouds/CloudSkyOcclusionPayload.hpp>
#include <Engine/Graphic/SkyPayload.hpp>

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

    // Set 0 of a GRAPHICS shader as the PIPELINE LAYOUT sees it: both stages reflected into one
    // ReflectionData, which is what VulkanShader does before it builds the VkDescriptorSetLayout. The
    // per-stage helpers above answer "what does this stage read"; this one answers "what shape must a
    // descriptor set have to be bindable to a pipeline built from this shader", and only the second
    // question can compare two different shaders.
    std::vector<VkDescriptorSetLayoutBinding> GraphicsSetZero( const std::filesystem::path& shaderFile )
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
        return it == data.ShaderDescriptorSets.end() ? std::vector<VkDescriptorSetLayoutBinding>{}
                                                     : ShaderReflection::BuildLayoutBindings( it->second );
    }

    // Printable "binding:type" list, so a failure names the slot that diverged instead of a count.
    std::string DescribeBindings( const std::vector<VkDescriptorSetLayoutBinding>& bindings )
    {
        std::ostringstream out;
        for ( const auto& b : bindings )
            out << b.binding << ':' << static_cast<int>( b.descriptorType ) << ' ';
        return out.str();
    }

    /**
     * The DECLARED SIZE of a storage block, in bytes, as the compiled SPIR-V says it is.
     *
     * WHY THIS EXISTS BESIDE ComputeSetZero, which already reflects the same module: the descriptor list
     * says a storage buffer is bound at a slot and says NOTHING about what is inside it. A parameter block
     * has two statements — a `struct` in C++ and a `layout(std430)` in GLSL — and every member after a
     * divergence is read from the wrong offset. That does not look like a bug; it looks like the clouds
     * being badly tuned, which is the sentence CloudPayload.hpp opens with.
     *
     * Returns 0 when the block is absent, which every caller treats as a failure rather than as a size.
     */
    uint32_t ComputeStorageBlockBytes( const std::filesystem::path& shaderFile, uint32_t binding )
    {
        const std::string source = StageSource( shaderFile, ShaderStage::Compute );
        const auto        spirv  = CompileStage( source, shaderFile, shaderc_compute_shader );
        if ( spirv.empty() )
            return 0u;

        ShaderResource::ReflectionData data;
        const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Compute, data );
        EXPECT_TRUE( diagnostics.empty() ) << ( diagnostics.empty() ? "" : diagnostics.front() );

        const auto set = data.ShaderDescriptorSets.find( 0 );
        if ( set == data.ShaderDescriptorSets.end() )
            return 0u;

        const auto block = set->second.StorageBuffers.find( binding );
        return block == set->second.StorageBuffers.end() ? 0u : block->second.Size;
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

TEST_F( ShaderRootFixture, TheCloudShadowMapDeclaresNineDescriptorsInSetZero )
{
    // THE PRODUCER OF THE CLOUD SHADOW MAP, and the reason it is pinned here rather than trusted: its
    // inputs are bound by NUMBER and not by reflection (ComputePipeline::SetInput takes the binding
    // as an argument), so the C++ constants in Engine/Graphic/Clouds/CloudShadowPayload.hpp and the
    // numbers in the shader are two statements of one fact. The assertions below are that fact, checked
    // against the compiled SPIR-V rather than against the file's text.
    //
    // The inputs are DELIBERATELY the march's own slot numbers — one vocabulary for one field — which is
    // why 3, 7, 8 and 9 are occupied and 1, 2, 4, 5, 6 are not.
    //
    // SIX SINCE SLOT A, and two of those are the ones a reader would not expect on a SHADOW pass: a hero
    // cloud shades the ground under it because it IS the cloud field, so the shadow march samples the
    // sculpted body through the same instance buffer and the same volume the view march does. Nothing was
    // added to the deferred pass to make that happen.
    //
    // NINE SINCE PHASE NV, and the three that arrived are noise volumes 1, 2 and 3. A layer carries four
    // cloud types and a type names its own volume; binding one of them was the programme's last recorded
    // debt and a dead setting in three slots of four. THE SHADOW PASS TAKES ALL FOUR TOO, and that is the
    // relation worth pinning here rather than the count: a cirrus eroded by the fine volume for the eye
    // and by the default one for the shadow map would be two different clouds in one frame.
    const auto bindings = ComputeSetZero( ShaderPath( "Clouds/CloudShadowMap.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 9u );

    for ( std::uint32_t slot = 0; slot < Desert::Graphic::kCloudSpeciesSlots; ++slot )
    {
        EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowNoiseBindings[slot],
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) )
             << "the shadow pass does not declare noise volume " << slot;
    }

    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowOutputBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ) ); // the triple it writes
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowParamsBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) ); // the cloud parameter block
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowNoiseBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the noise volume
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowModellingBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the procedural modelling volume
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowAuthoredBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) ); // the hero cloud instances
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudShadowAuthoredAtlasBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the sculpted body
}

TEST_F( ShaderRootFixture, TheCloudMarchDeclaresFifteenDescriptorsInSetZero )
{
    // THE VIEW MARCH, pinned on the same terms and for the same reason, and it was NOT pinned before slot
    // A landed — which is precisely why it is worth doing now: two of its ten descriptors are new, both
    // are bound by number, and one of them is a `sampler3D` that MUST be bound even when nothing reads
    // it. An unbound sampler is an invalid descriptor set, and this backend answers an invalid set by
    // silently skipping the dispatch: every cloud in the frame would vanish with nothing in the log.
    // That failure has happened in this subsystem before, which is what makes a count a useful assertion.
    //
    // THIRTEEN SINCE PHASE NV. The three that arrived are noise volumes 1, 2 and 3, and they are bound on
    // exactly the terms the note above describes: ALWAYS, even when the layer needs one volume, because
    // Graphic::ResolveCloudNoiseVolumes fills the unused slots with slot 0's image rather than leaving
    // them empty. They cost nothing in memory — Assets::AssetPreloader uploads every `.dcnv` in the
    // project whatever any scene names — and they are what makes a type's own volume reach the frame.
    //
    // FOURTEEN SINCE Р4. The one that arrived is the SKY-LIGHT OCCLUSION VOLUME at binding 13, and it is
    // bound on exactly the terms this note describes: ALWAYS, fallback included, even in the default scene
    // where the layer's flag is off and CloudPush::Frame.x tells the march not to read it.
    //
    // FIFTEEN SINCE Р14. The one that arrived is the ATMOSPHERE'S TRANSMITTANCE LUT at binding 14 — the
    // SKY's texture, read by the cloud march so that the sun's colour can be re-evaluated at each sample's
    // own altitude instead of once at sea level. Bound on the same terms again, and in the shipped scene
    // it is the FALLBACK that is bound, because the field it serves is off by default.
    const auto bindings = ComputeSetZero( ShaderPath( "Clouds/CloudRaymarch.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 15u );

    for ( std::uint32_t slot = 0; slot < Desert::Graphic::kCloudSpeciesSlots; ++slot )
    {
        EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudNoiseBindings[slot],
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) )
             << "the march does not declare noise volume " << slot;
    }

    // AND THE TWO PASSES USE ONE VOCABULARY FOR ONE FIELD. Stated as an assertion rather than as an alias
    // in a header nobody re-reads: the shadow map binds by number too, and a march that read volume 2 from
    // descriptor 11 while the shadow pass read it from 12 would give a cloud a shadow cut from a different
    // noise, which is not an error anywhere — it is a shadow that does not fit its cloud.
    for ( std::uint32_t slot = 0; slot < Desert::Graphic::kCloudSpeciesSlots; ++slot )
    {
        EXPECT_EQ( Desert::Graphic::kCloudNoiseBindings[slot], Desert::Graphic::kCloudShadowNoiseBindings[slot] );
    }

    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudOutputBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ) ); // the scatter target
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudGuideOutputBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ) ); // the depth guide beside it
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudParamsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudSceneDepthBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE(
         HasBinding( bindings, Desert::Graphic::kCloudNoiseBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudDistantSkyLightBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudAerialPerspectiveBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudModellingBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudAuthoredBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) ); // the hero cloud instances
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudAuthoredAtlasBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the sculpted body
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudSkyOcclusionBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the sky-light occlusion volume
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kCloudSunTransmittanceLutBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // the atmosphere transmittance LUT

    // AND IT MUST NOT COLLIDE WITH ANYTHING ALREADY THERE. Stated as an assertion because the bindings are
    // handed to SetInput verbatim and a duplicate number does not fail anywhere: it lands one resource on
    // top of another, and what the march then samples is whichever of the two the renderer bound last.
    for ( const std::uint32_t taken :
          { Desert::Graphic::kCloudOutputBinding, Desert::Graphic::kCloudParamsBinding,
            Desert::Graphic::kCloudSceneDepthBinding, Desert::Graphic::kCloudNoiseBinding,
            Desert::Graphic::kCloudDistantSkyLightBinding, Desert::Graphic::kCloudAerialPerspectiveBinding,
            Desert::Graphic::kCloudGuideOutputBinding, Desert::Graphic::kCloudModellingBinding,
            Desert::Graphic::kCloudAuthoredBinding, Desert::Graphic::kCloudAuthoredAtlasBinding,
            Desert::Graphic::kCloudNoiseBindings[1], Desert::Graphic::kCloudNoiseBindings[2],
            Desert::Graphic::kCloudNoiseBindings[3], Desert::Graphic::kCloudSkyOcclusionBinding } )
    {
        EXPECT_NE( Desert::Graphic::kCloudSunTransmittanceLutBinding, taken );
    }
}

TEST_F( ShaderRootFixture, TheSkyOcclusionVolumesProducerAndConsumerAgreeAboutTheFieldTheyIntegrate )
{
    // THE RELATION THAT MAKES THE VOLUME MEAN ANYTHING. A column is integrated by one shader and read by
    // another, and what makes the number correct is that BOTH sampled the same field: the same four noise
    // volumes, the same modelling volume, the same hero-cloud atlas and instance list. A producer that
    // eroded its column from a different volume than the eye's would darken clouds the frame does not
    // contain — not an error anywhere, just a shaded side that does not fit its cloud, which is the exact
    // failure shape the shadow map's own binding assertions exist to catch.
    const auto producer = ComputeSetZero( ShaderPath( "Clouds/CloudSkyOcclusionVolume.shader" ) );

    EXPECT_TRUE( HasBinding( producer, Desert::Graphic::kCloudSkyOcclusionOutputBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ) ); // the volume it writes
    EXPECT_TRUE( HasBinding( producer, Desert::Graphic::kCloudSkyOcclusionParamsBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );
    EXPECT_TRUE( HasBinding( producer, Desert::Graphic::kCloudSkyOcclusionModellingBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( producer, Desert::Graphic::kCloudSkyOcclusionAuthoredBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );
    EXPECT_TRUE( HasBinding( producer, Desert::Graphic::kCloudSkyOcclusionAuthoredAtlasBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );

    for ( std::uint32_t slot = 0; slot < Desert::Graphic::kCloudSpeciesSlots; ++slot )
    {
        EXPECT_TRUE( HasBinding( producer, Desert::Graphic::kCloudSkyOcclusionNoiseBindings[slot],
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) )
             << "the sky-light occlusion producer does not declare noise volume " << slot;

        // ONE VOCABULARY FOR ONE FIELD, asserted rather than left to an alias nobody re-reads.
        EXPECT_EQ( Desert::Graphic::kCloudNoiseBindings[slot],
                   Desert::Graphic::kCloudSkyOcclusionNoiseBindings[slot] );
    }

    // AND IT READS THE SAME PARAMETER BLOCK, byte for byte. It is handed the very buffer the march is
    // handed — one upload, two dispatches — so a block of a different size here would be every field after
    // the divergence read from the wrong offset in a pass whose output is then trusted by the other.
    const uint32_t bytes = ComputeStorageBlockBytes( ShaderPath( "Clouds/CloudSkyOcclusionVolume.shader" ),
                                                     Desert::Graphic::kCloudParamsBinding );
    EXPECT_EQ( bytes, sizeof( Desert::Graphic::CloudGpuPayload ) );
}

TEST_F( ShaderRootFixture, TheCloudParameterBlockIsTheSameNumberOfBytesOnBothSidesOfTheWire )
{
    // THE RELATION THE static_asserts IN CloudPayload.hpp CANNOT REACH. They pin the C++ struct's offsets
    // against themselves, which catches half a move inside one file and nothing at all about the GLSL
    // block that reads those bytes — and the two are edited in different files, in different languages, by
    // whoever adds a parameter. A member added on one side only shifts every member after it, and the
    // symptom is not an error: it is a sky that looks badly tuned.
    //
    // IT IS THE SIZE AND NOT THE OFFSETS, and the difference is what spirv-cross gives for a STORAGE block
    // — VulkanShaderReflection fills member offsets for uniform buffers and only the declared size for
    // storage buffers, and this block is a storage buffer. The size is the weaker statement of the two and
    // it is the one available without changing the engine's reflection, which is another task's file. It
    // catches exactly the failure this phase could have introduced: SpeciesNoise added to one side only.
    //
    // BOTH PASSES, because both include Common/CloudParams.glslh and both are handed the SAME bytes by
    // VolumetricCloudRenderer — the shadow map's block and the march's block are one struct written twice.
    for ( const char* shader : { "Clouds/CloudRaymarch.shader", "Clouds/CloudShadowMap.shader" } )
    {
        const uint32_t bytes =
             ComputeStorageBlockBytes( ShaderPath( shader ), Desert::Graphic::kCloudParamsBinding );

        EXPECT_GT( bytes, 0u ) << shader << " declares no storage block at the cloud parameter binding";
        EXPECT_EQ( bytes, sizeof( Desert::Graphic::CloudGpuPayload ) )
             << shader << " reads a parameter block of " << bytes << " bytes where Graphic::CloudGpuPayload "
             << "is " << sizeof( Desert::Graphic::CloudGpuPayload )
             << " — every member after the divergence is read from the wrong offset";
    }
}

TEST_F( ShaderRootFixture, TheDeferredLightingPassDeclaresTwentyDescriptorsInSetZero )
{
    // THE CONSUMER, and the pass this repository shares most widely — every deferred scene draws it, and
    // it is the one file the cloud work was told to touch as little as possible. Pinning its descriptor
    // set is how "as little as possible" becomes checkable: the count moves the day somebody adds a
    // binding to it, whether or not they meant to.
    //
    // Twenty. Slots 0..16 with none free in between: six G-buffer and scene samplers (1, 2, 3, 8, 9, 10),
    // four cascade maps (5, 13, 14, 15), four uniform blocks (0, 4, 7, 12), two light SSBOs (6, 16) and
    // the cloud shadow map (11) — plus the three the ambient needs (17, 18, 19). Was seventeen until
    // 2026-09-03, when this pass stopped inventing its ambient out of a flat constant and started reading
    // the same baked environment the forward mesh shaders read.
    const auto bindings = FragmentSetZero( ShaderPath( "Deferred/DeferredLighting.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 20u );

    EXPECT_TRUE( HasBinding( bindings, 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_CloudShadowMap
    EXPECT_TRUE( HasBinding( bindings, 12, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );         // CloudShadowUB

    // THE AMBIENT'S OWN THREE, and the point of the whole change: a deferred composite that declares
    // none of these cannot be reading the sky, whatever its ambient line says it is doing. That was
    // exactly the state the owner saw as black ground under a bright sky.
    EXPECT_TRUE( HasBinding( bindings, 17, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_EnvIrradianceTex
    EXPECT_TRUE( HasBinding( bindings, 18, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_EnvSpecularTex
    EXPECT_TRUE( HasBinding( bindings, 19, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_BRDFLUTTexture

    // And the cascade block it must NOT have disturbed: the same four maps at the same four slots, with
    // ShadowUB where PBR.glsl.frag mirrors it.
    EXPECT_TRUE( HasBinding( bindings, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 14, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 15, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );
}

TEST_F( ShaderRootFixture, TheGBufferShaderKeepsTheForwardMeshShadersDescriptorLayoutExactly )
{
    // WHY StaticMeshGBuffer.shader multiplies three environment samples, four cascade maps and two light
    // SSBOs by 1e-20 — the strangest-looking lines in the renderer, and the ones most likely to be
    // deleted as dead by someone tidying up.
    //
    // The deferred G-buffer pass does NOT get a material of its own. MeshRenderer draws it with the
    // (Static x Forward) material, whose descriptor sets were allocated from the FORWARD shader's
    // reflection (Graphic::MeshShaderFor(Static, Forward) == "StaticMeshPBR"), and binds them against a
    // pipeline layout built from THIS shader's reflection — MeshRenderer::DrawStaticMeshes picks
    // m_StaticGBufferPipeline for the same executor. Vulkan requires those two layouts to be compatible.
    //
    // Note that (Static x GBuffer) IS a real variant now and has its own material class instance (the RSM
    // pass uses exactly that pair). What has NOT changed is which material the G-buffer pass binds: it
    // still borrows the forward one's sets, so the dummy below is still load-bearing. Giving the pass its
    // own material is the change that would retire it, and it has not been made.
    // An unreferenced uniform does not survive SPIR-V reflection, so the only way for the G-buffer shader
    // to keep a slot it has no use for is to touch it — hence a sum scaled into oblivion and folded into
    // an output so the optimiser cannot fold it back out.
    //
    // That makes the dummy a RELATION, not a workaround, and this is that relation asserted. It is not
    // affected by the deferred composite gaining its own environment bindings: the composite is a
    // separate fullscreen material with a separate layout, and the G-buffer pass still borrows the
    // forward material's set. Delete the `keep` lines and this test fails with the slot that vanished —
    // instead of the validation layer failing on a machine that happens to have layers on.
    const auto forward = GraphicsSetZero( ShaderPath( "PBR/StaticMeshPBR.shader" ) );
    const auto gbuffer = GraphicsSetZero( ShaderPath( "PBR/StaticMeshGBuffer.shader" ) );

    ASSERT_FALSE( forward.empty() );
    ASSERT_FALSE( gbuffer.empty() );

    EXPECT_EQ( DescribeBindings( gbuffer ), DescribeBindings( forward ) )
         << "StaticMeshGBuffer's set 0 no longer matches StaticMeshPBR's, but MeshRenderer still binds "
            "one material's descriptor sets to both pipelines";
    EXPECT_EQ( ShaderReflection::CountDescriptors( gbuffer ), ShaderReflection::CountDescriptors( forward ) );
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

TEST_F( ShaderRootFixture, TheEnvironmentBakeReadsTheSkyAndTheCloudsOnTwoDIFFERENTDESCRIPTORS )
{
    // THE MINE THIS TEST EXISTS FOR, and it is one line of arithmetic rather than a rendering failure.
    // Graphic::kSkyPayloadBinding is 1, and Common/CloudParams.glslh's CLOUD_PARAMS_BINDING defaults to 1
    // as well. The environment bake is the ONE pass in the engine that reads both blocks, so if the cloud
    // header's number were not overridable the two would land on one descriptor — and the symptom is not
    // an error but one of the blocks reading the other's bytes, which renders as a sky whose clouds are
    // shaped by the atmosphere's ozone density.
    //
    // Both numbers below come from the C++ constants, and the reflection reads the SHADER, so this
    // asserts the two statements of the layout against each other rather than either against a literal.
    ASSERT_NE( Desert::Graphic::kSkyBakeCloudParamsBinding, Desert::Graphic::kSkyPayloadBinding )
         << "the bake would bind the cloud block and the sky block to one descriptor";

    const auto bindings = ComputeSetZero( ShaderPath( "Compute/BakeProceduralSky.shader" ) );

    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyPayloadBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) ); // the sky payload
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyBakeCloudParamsBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) ); // the cloud payload
}

TEST_F( ShaderRootFixture, TheEnvironmentBakeMarchesTheSameFIELDTheScreenMarchDoes )
{
    // THE RELATION, not the count. What makes the baked environment agree with the visible sky is that
    // both marches sample the SAME field: the same four noise volumes, the same modelling volume, the
    // same hero-cloud atlas and instance list, the same sky-light occlusion volume. A bake that erased
    // one of them would light the world with a sky nobody can see — no error anywhere, just an ambient
    // that does not fit the frame, which is the failure shape the shadow map's own binding assertions
    // exist to catch.
    //
    // The NUMBERS differ from the march's on purpose (the bake's set already holds the sky's own four
    // descriptors), so what is asserted is that every resource the march binds has a counterpart here and
    // that the counterpart is the constant C++ hands to SetInput.
    const auto bindings = ComputeSetZero( ShaderPath( "Compute/BakeProceduralSky.shader" ) );

    for ( std::uint32_t slot = 0; slot < Desert::Graphic::kCloudSpeciesSlots; ++slot )
    {
        EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyBakeCloudNoiseBindings[slot],
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) )
             << "noise volume " << slot;
    }

    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyBakeCloudModellingBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyBakeCloudAuthoredAtlasBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyBakeCloudAuthoredBinding,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyBakeDistantSkyLightBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_TRUE( HasBinding( bindings, Desert::Graphic::kSkyBakeCloudSkyOcclusionBinding,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );

    // EVERY DESCRIPTOR IS DISTINCT, which is the property the two blocks above nearly broke and which no
    // amount of per-binding checking states: the reflection would happily report one binding satisfying
    // two of the expectations above.
    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), bindings.size() );
}

// ---- The shader graph's lit surface against the standard mesh shader --------------------------------

namespace
{
    // Is @p include one of the engine's SHARED SHADING TEXTS — the files whose whole purpose is that
    // every shader that shades a surface compiles the same one?
    //
    // DERIVED, not typed. Everything under Resources/Shaders/Mesh is such a text by construction: that
    // directory holds the ambient (AmbientIBL.glslh), the direct-light BRDF (DirectLighting.glslh), the
    // two punctual light models and the BRDF pieces they share, and nothing else has ever been put there.
    // Common/CloudShadowReceiver.glslh is named on top because it is the same kind of file and lives in
    // Common only under duress — its producer includes Common/CloudShadowMap.glslh and declares
    // u_CloudShadowMap as a writeonly image2D, so the receiver cannot sit beside it.
    //
    // A typed list would drift, and drifting is the whole failure mode: the next shared text somebody
    // extracts must reach the graph too, and nobody will remember to add it here.
    bool IsSharedShadingText( const std::filesystem::path& include )
    {
        if ( include.parent_path().filename() == "Mesh" )
            return true;
        return include.filename() == "CloudShadowReceiver.glslh";
    }

    std::vector<std::filesystem::path> FragmentIncludes( const std::filesystem::path& shaderFile )
    {
        return CollectShaderIncludes( StageSource( shaderFile, ShaderStage::Fragment ), shaderFile );
    }
} // namespace

TEST_F( ShaderRootFixture, ALitGraphSurfaceCompilesEverySharedShadingTextTheMeshShaderDoes )
{
    // THE RELATION Д16 exists to assert, and it is deliberately not "the graph calls AmbientIBL".
    //
    // A shader-graph material is a surface an artist ticked "Lit" on. Until this test, ticking it produced
    // a lighting model the graph COMPILER wrote into the generated file: a flat `vec3( 0.12 )` ambient
    // that read no environment, a Lambert term that did not divide albedo by PI, and no cloud shadow at
    // all — three defects the engine had already fixed, in Р16, Р20 and Р21 respectively, by making each
    // term ONE text. The graph was a fourth copy that nobody had migrated, so all three lived on in it.
    //
    // What is asserted is therefore membership, not behaviour: every shared shading text the standard mesh
    // shader compiles, the lit graph surface compiles as well. That fails the day somebody extracts a new
    // one into Mesh/ and wires it into StaticMeshPBR only — which is exactly how the graph fell behind the
    // first time.
    const auto mesh  = FragmentIncludes( ShaderPath( "PBR/StaticMeshPBR.shader" ) );
    const auto graph = FragmentIncludes( ShaderPath( "Graph/MatLitConst.shader" ) );

    ASSERT_FALSE( mesh.empty() );
    ASSERT_FALSE( graph.empty() );

    const auto compiles = [&graph]( const std::filesystem::path& header )
    {
        for ( const auto& include : graph )
            if ( include.filename() == header.filename() )
                return true;
        return false;
    };

    int shared = 0;
    for ( const auto& include : mesh )
    {
        if ( !IsSharedShadingText( include ) )
            continue;
        ++shared;
        EXPECT_TRUE( compiles( include ) )
             << "StaticMeshPBR shades with " << include.filename().string()
             << " and the lit shader-graph surface does not — a graph material is lit by a model of its "
                "own again";
    }

    // Vacuous success is the failure mode of every membership test: a closure that came back empty, or a
    // Mesh/ that stopped being where the shared texts live, would pass the loop above without checking
    // anything. Seven is what StaticMeshPBR's closure names today — six after Д16, plus
    // Mesh/CascadedShadow.glslh, which is exactly the automatic membership this test was written to give:
    // Д20 extracted ShadowFactor into Mesh/ and the graph was REQUIRED to compile it without a line of
    // this test changing. The assertion is a floor rather than an equality so that adding the next shared
    // text is not, by itself, a broken test.
    EXPECT_GE( shared, 7 ) << "the mesh shader's closure no longer names the shared shading texts";
}

TEST_F( ShaderRootFixture, TheLitGraphSurfaceIsHANDEDTheSceneITSHADESWITH )
{
    // The other half of the same relation: compiling the shared texts is worth nothing if the descriptors
    // they read are not in the set. Each of these is a resource the graph's generated shader could not
    // have received before — MeshRenderer's generic path filled CameraUB, TimeUB and DirectionLightsUB by
    // hand and nothing else, so an environment cube or a cloud shadow map had no route to a data-driven
    // material at all.
    //
    // Numbers, not names, because that is what a descriptor set is; they are the SAME numbers the four
    // mesh shaders use for the same things, which is a property worth keeping even though every material
    // in this engine binds by name.
    const auto bindings = GraphicsSetZero( ShaderPath( "Graph/MatLitConst.shader" ) );
    ASSERT_FALSE( bindings.empty() );

    EXPECT_TRUE( HasBinding( bindings, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );  // u_EnvSpecularTex
    EXPECT_TRUE( HasBinding( bindings, 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );  // u_EnvIrradianceTex
    EXPECT_TRUE( HasBinding( bindings, 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_BRDFLUTTexture
    EXPECT_TRUE( HasBinding( bindings, 20, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_CloudShadowMap
    EXPECT_TRUE( HasBinding( bindings, 21, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );         // CloudShadowUB
    EXPECT_TRUE( HasBinding( bindings, 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );          // LightsMetadata
    EXPECT_TRUE( HasBinding( bindings, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );          // PointLightsUB
    EXPECT_TRUE( HasBinding( bindings, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ) );         // SpotLightsUB
    EXPECT_TRUE( HasBinding( bindings, 14, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );         // DirectionLightsUB

    // The five cascade bindings Д20 added. Two of them are NOT at the mesh shaders' numbers and cannot be:
    // 14 and 15 hold DirectionLightsUB and TimeUB in a shader-graph layout, which no mesh shader declares.
    // The NAMES are what the engine binds by, and Tests/Engine/PBRSceneFrame asserts those against the C++
    // writer for every consumer of the shared text; what is pinned here is that the numbers this layout
    // chose are the ones it still has.
    EXPECT_TRUE( HasBinding( bindings, 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) );          // ShadowUB
    EXPECT_TRUE( HasBinding( bindings, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );  // u_ShadowMap0
    EXPECT_TRUE( HasBinding( bindings, 13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_ShadowMap1
    EXPECT_TRUE( HasBinding( bindings, 22, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_ShadowMap2
    EXPECT_TRUE( HasBinding( bindings, 23, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) ); // u_ShadowMap3

    // The graph's own textures start at kGraphTextureBinding (24) and count upward, so none of the slots
    // above can be taken by a Properties block however many textures an artist adds. Distinctness is the
    // property that says so, and it is the one a per-slot check cannot state.
    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), bindings.size() );
}

TEST_F( ShaderRootFixture, AnUnlitGraphSurfaceReceivesNoneOfIt )
{
    // The boundary is real and not a matter of degree: an unlit graph shader is a DIFFERENT domain of
    // shading, it declares no lighting resource at all, and the change that gave the lit branch the
    // engine's model must not have quietly given the unlit branch a lighting descriptor it will never
    // read. (The Metallic/Roughness/Occlusion pins are likewise not evaluated for an unlit graph — the
    // nodes behind them would otherwise be emitted into a shader that discards the result.)
    const auto includes = FragmentIncludes( ShaderPath( "Graph/MatConst.shader" ) );
    for ( const auto& include : includes )
        EXPECT_FALSE( IsSharedShadingText( include ) )
             << "an unlit graph surface compiles " << include.filename().string();

    const auto bindings = GraphicsSetZero( ShaderPath( "Graph/MatConst.shader" ) );
    EXPECT_FALSE( HasBinding( bindings, 20, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
    EXPECT_FALSE( HasBinding( bindings, 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ) );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
