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
#include <Engine/Graphic/Clouds/CloudShadowPayload.hpp>
#include <Engine/Graphic/Clouds/CloudSkyOcclusionPayload.hpp>

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

TEST_F( ShaderRootFixture, TheCloudMarchDeclaresFourteenDescriptorsInSetZero )
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
    // where the layer's flag is off and CloudPush::SkyOcclusion.x tells the march not to read it.
    const auto bindings = ComputeSetZero( ShaderPath( "Clouds/CloudRaymarch.shader" ) );

    EXPECT_EQ( ShaderReflection::CountDescriptors( bindings ), 14u );

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
