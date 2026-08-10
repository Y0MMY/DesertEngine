// CLD-11 — SPIR-V reflection must classify image resources by their declared TYPE.
//
// The rule this replaced looked at the variable's NAME: a resource whose name contained "Env" or
// "Cube" was filed as a cube sampler, anything else as a 2D one, and every storage image as a 2D
// storage image. Nothing about that fails loudly — the descriptor TYPE it produces is right, only the
// view type is wrong — so a `sampler3D` bound as 2D just returns nonsense and the render is merely
// "off". These tests are the regression guard: the resource names below are deliberately chosen so
// that name matching gets every single one of them wrong.

#include <gtest/gtest.h>

#include <Engine/Graphic/API/Vulkan/VulkanShaderReflection.hpp>

#include <shaderc/shaderc.hpp>

#include <string>
#include <vector>

using namespace Desert::Graphic::API::Vulkan;
using Desert::Core::Formats::ShaderStage;

namespace
{
    std::vector<uint32_t> Compile( const char* source, shaderc_shader_kind kind )
    {
        shaderc::Compiler       compiler;
        shaderc::CompileOptions options;
        // Same target as Core::ShaderCompiler::CompileGLSLToSPIRV, so the SPIR-V under test is shaped
        // like the SPIR-V the engine actually reflects.
        options.SetTargetEnvironment( shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1 );

        const auto result = compiler.CompileGlslToSpv( source, kind, "shader_reflection_test.glsl", options );
        EXPECT_EQ( result.GetCompilationStatus(), shaderc_compilation_status_success ) << result.GetErrorMessage();
        if ( result.GetCompilationStatus() != shaderc_compilation_status_success )
        {
            return {};
        }
        return { result.begin(), result.end() };
    }

    std::string FirstOr( const std::vector<std::string>& messages, const char* fallback )
    {
        return messages.empty() ? fallback : messages.front();
    }

    // All five image declarations at once, which is exactly what CLD-11 asks to be shown apart.
    // u_EnvNoise is a sampler2D and u_Radiance a samplerCube: under name matching the first would be
    // filed as a cube and the second as 2D, so this shader alone fails if the old rule comes back.
    const char* kAllFiveImageKinds = R"(#version 450
layout(binding = 0) uniform sampler2D   u_EnvNoise;
layout(binding = 1) uniform sampler3D   u_ShapeNoise;
layout(binding = 2) uniform samplerCube u_Radiance;
layout(binding = 3, rgba8) writeonly uniform image2D u_OutSlice;
layout(binding = 4, rgba8) writeonly uniform image3D u_OutVolume;

layout(binding = 5) uniform Params { vec4 u_Bounds; };
layout(push_constant) uniform Push { vec4 u_Extent; };

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
void main()
{
    vec4 c = texture(u_EnvNoise, vec2(0.5))
           + texture(u_ShapeNoise, vec3(0.5))
           + texture(u_Radiance, vec3(0.0, 1.0, 0.0))
           + u_Bounds + u_Extent;
    imageStore(u_OutSlice,  ivec2(gl_GlobalInvocationID.xy), c);
    imageStore(u_OutVolume, ivec3(gl_GlobalInvocationID),    c);
}
)";
} // namespace

TEST( ShaderReflection, FiveImageKindsLandInFiveDistinctBuckets )
{
    const auto spirv = Compile( kAllFiveImageKinds, shaderc_glsl_compute_shader );
    ASSERT_FALSE( spirv.empty() );

    ShaderResource::ReflectionData data;
    const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Compute, data );
    ASSERT_TRUE( diagnostics.empty() ) << FirstOr( diagnostics, "" );

    ASSERT_EQ( data.ShaderDescriptorSets.size(), 1u );
    const auto& set0 = data.ShaderDescriptorSets.at( 0 );

    ASSERT_EQ( set0.Image2DSamplers.size(), 1u );
    ASSERT_EQ( set0.Image3DSamplers.size(), 1u );
    ASSERT_EQ( set0.ImageCubeSamplers.size(), 1u );
    ASSERT_EQ( set0.StorageImage2DSamplers.size(), 1u );
    ASSERT_EQ( set0.StorageImage3DSamplers.size(), 1u );

    EXPECT_EQ( set0.Image2DSamplers.at( 0 ).Name, "u_EnvNoise" );
    EXPECT_EQ( set0.Image3DSamplers.at( 1 ).Name, "u_ShapeNoise" );
    EXPECT_EQ( set0.ImageCubeSamplers.at( 2 ).Name, "u_Radiance" );
    EXPECT_EQ( set0.StorageImage2DSamplers.at( 3 ).Name, "u_OutSlice" );
    EXPECT_EQ( set0.StorageImage3DSamplers.at( 4 ).Name, "u_OutVolume" );

    EXPECT_EQ( set0.Image3DSamplers.at( 1 ).BindingPoint, 1u );
    EXPECT_EQ( set0.StorageImage3DSamplers.at( 4 ).BindingPoint, 4u );
}

// The reflection also carries buffers and push constants; it moved translation unit wholesale when it
// was made device-free, so assert it still brings them along.
TEST( ShaderReflection, KeepsUniformBuffersAndPushConstants )
{
    const auto spirv = Compile( kAllFiveImageKinds, shaderc_glsl_compute_shader );
    ASSERT_FALSE( spirv.empty() );

    ShaderResource::ReflectionData data;
    const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Compute, data );
    ASSERT_TRUE( diagnostics.empty() ) << FirstOr( diagnostics, "" );

    const auto& set0 = data.ShaderDescriptorSets.at( 0 );
    ASSERT_EQ( set0.UniformBuffers.size(), 1u );

    const auto& ub = set0.UniformBuffers.at( 5 );
    EXPECT_EQ( ub.Name, "Params" );
    EXPECT_EQ( ub.Size, 16u );
    EXPECT_EQ( ub.ShaderStage, ShaderStage::Compute );
    ASSERT_EQ( ub.Fields.size(), 1u );
    EXPECT_EQ( ub.Fields[0].Name, "u_Bounds" );
    EXPECT_EQ( ub.Fields[0].Offset, 0u );

    ASSERT_TRUE( data.PushConstantRanges.has_value() );
    EXPECT_EQ( data.PushConstantRanges->Size, 16u );
    EXPECT_EQ( data.PushConstantRanges->ShaderStage, ShaderStage::Compute );
}

// The sharpest form of the regression guard: names that say the opposite of the truth. `u_EnvCubeMap`
// contains BOTH substrings the old rule keyed on and is a plain sampler2D; `u_Density` contains
// neither and is a cube. Restore name matching and both of these flip.
TEST( ShaderReflection, ClassificationIgnoresResourceNames )
{
    const char* kMisleadingNames = R"(#version 450
layout(binding = 0) uniform sampler2D   u_EnvCubeMap;
layout(binding = 1) uniform samplerCube u_Density;
layout(location = 0) out vec4 o_Color;
void main()
{
    o_Color = texture(u_EnvCubeMap, vec2(0.5)) + texture(u_Density, vec3(0.0, 1.0, 0.0));
}
)";

    const auto spirv = Compile( kMisleadingNames, shaderc_glsl_fragment_shader );
    ASSERT_FALSE( spirv.empty() );

    ShaderResource::ReflectionData data;
    const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Fragment, data );
    ASSERT_TRUE( diagnostics.empty() ) << FirstOr( diagnostics, "" );

    const auto& set0 = data.ShaderDescriptorSets.at( 0 );
    ASSERT_EQ( set0.Image2DSamplers.size(), 1u );
    ASSERT_EQ( set0.ImageCubeSamplers.size(), 1u );
    EXPECT_EQ( set0.Image2DSamplers.at( 0 ).Name, "u_EnvCubeMap" );
    EXPECT_EQ( set0.ImageCubeSamplers.at( 1 ).Name, "u_Density" );
}

// A stage that declares an image the engine cannot bind must be reported by name and type, and must
// end up in NO bucket. Filing it under its base dimension is what the old rule effectively did.
TEST( ShaderReflection, RefusesArrayedImagesInsteadOfGuessing )
{
    const char* kArrayed = R"(#version 450
layout(binding = 0) uniform sampler2DArray u_Slices;
layout(location = 0) out vec4 o_Color;
void main() { o_Color = texture(u_Slices, vec3(0.5)); }
)";

    const auto spirv = Compile( kArrayed, shaderc_glsl_fragment_shader );
    ASSERT_FALSE( spirv.empty() );

    ShaderResource::ReflectionData data;
    const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Fragment, data );

    ASSERT_EQ( diagnostics.size(), 1u );
    EXPECT_NE( diagnostics.front().find( "u_Slices" ), std::string::npos ) << diagnostics.front();
    EXPECT_NE( diagnostics.front().find( "sampler2DArray" ), std::string::npos ) << diagnostics.front();

    for ( const auto& [set, descriptorSet] : data.ShaderDescriptorSets )
    {
        EXPECT_TRUE( descriptorSet.Image2DSamplers.empty() ) << "set " << set;
        EXPECT_TRUE( descriptorSet.Image3DSamplers.empty() ) << "set " << set;
        EXPECT_TRUE( descriptorSet.ImageCubeSamplers.empty() ) << "set " << set;
    }
}

// An ARRAY of samplers is a different thing from an arrayed image, and equally unbindable here: every
// descriptor-set layout the engine builds hardcodes descriptorCount = 1.
TEST( ShaderReflection, RefusesArraysOfDescriptors )
{
    const char* kDescriptorArray = R"(#version 450
layout(binding = 0) uniform sampler2D u_Textures[4];
layout(location = 0) out vec4 o_Color;
void main() { o_Color = texture(u_Textures[1], vec2(0.5)); }
)";

    const auto spirv = Compile( kDescriptorArray, shaderc_glsl_fragment_shader );
    ASSERT_FALSE( spirv.empty() );

    ShaderResource::ReflectionData data;
    const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Fragment, data );

    ASSERT_EQ( diagnostics.size(), 1u );
    EXPECT_NE( diagnostics.front().find( "u_Textures" ), std::string::npos ) << diagnostics.front();

    // The only resource in the shader was refused, so no set was ever touched.
    EXPECT_TRUE( data.ShaderDescriptorSets.empty() );
}

// Storage 2D and storage cube deliberately share one bucket — a storage binding is written from a mip
// view that already carries its own view type. Asserted so the sharing stays a decision, not a
// leftover: the IBL compute chain (PanoramaToCubemap, DiffuseIrradiance, PrefilterEnvMap) depends on
// an `imageCube` output continuing to land exactly where it lands today.
TEST( ShaderReflection, StorageCubeSharesTheStorage2DBucket )
{
    const char* kCubeOutput = R"(#version 450
layout(set = 0, binding = 0) uniform samplerCube inputTexture;
layout(set = 0, binding = 1, rgba32f) restrict writeonly uniform imageCube outputTexture;
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
    imageStore(outputTexture, ivec3(gl_GlobalInvocationID),
               texture(inputTexture, vec3(0.0, 1.0, 0.0)));
}
)";

    const auto spirv = Compile( kCubeOutput, shaderc_glsl_compute_shader );
    ASSERT_FALSE( spirv.empty() );

    ShaderResource::ReflectionData data;
    const auto diagnostics = ShaderReflection::ReflectStage( spirv, ShaderStage::Compute, data );
    ASSERT_TRUE( diagnostics.empty() ) << FirstOr( diagnostics, "" );

    const auto& set0 = data.ShaderDescriptorSets.at( 0 );
    ASSERT_EQ( set0.StorageImage2DSamplers.size(), 1u );
    EXPECT_EQ( set0.StorageImage2DSamplers.at( 1 ).Name, "outputTexture" );
    EXPECT_TRUE( set0.StorageImage3DSamplers.empty() );

    // ...while the sampled cube DOES move: `inputTexture` names neither "Env" nor "Cube", so the old
    // rule filed this real samplerCube as a 2D sampler. This is PrefilterEnvMap.shader, verbatim.
    ASSERT_EQ( set0.ImageCubeSamplers.size(), 1u );
    EXPECT_EQ( set0.ImageCubeSamplers.at( 0 ).Name, "inputTexture" );
    EXPECT_TRUE( set0.Image2DSamplers.empty() );
}

TEST( ShaderReflection, MergesStageMasksAcrossStages )
{
    const char* kVertex   = R"(#version 450
layout(binding = 0) uniform sampler2D u_Shared;
void main() { gl_Position = texture(u_Shared, vec2(0.5)); }
)";
    const char* kFragment = R"(#version 450
layout(binding = 0) uniform sampler2D u_Shared;
layout(location = 0) out vec4 o_Color;
void main() { o_Color = texture(u_Shared, vec2(0.5)); }
)";

    ShaderResource::ReflectionData data;
    ASSERT_TRUE( ShaderReflection::ReflectStage( Compile( kVertex, shaderc_glsl_vertex_shader ),
                                                 ShaderStage::Vertex, data )
                      .empty() );
    ASSERT_TRUE( ShaderReflection::ReflectStage( Compile( kFragment, shaderc_glsl_fragment_shader ),
                                                 ShaderStage::Fragment, data )
                      .empty() );

    const auto& set0 = data.ShaderDescriptorSets.at( 0 );
    ASSERT_EQ( set0.Image2DSamplers.size(), 1u ); // one descriptor, not one per stage

    const uint32_t stages = (uint32_t)set0.Image2DSamplers.at( 0 ).ShaderStage;
    EXPECT_TRUE( stages & (uint32_t)ShaderStage::Vertex );
    EXPECT_TRUE( stages & (uint32_t)ShaderStage::Fragment );
}

TEST( ShaderReflection, ClassifiesDimensionsDirectly )
{
    const auto spirv = Compile( kAllFiveImageKinds, shaderc_glsl_compute_shader );
    ASSERT_FALSE( spirv.empty() );

    spirv_cross::CompilerGLSL compiler( spirv );
    const auto                resources = compiler.get_shader_resources();

    for ( const auto& resource : resources.sampled_images )
    {
        const auto& type      = compiler.get_type( resource.base_type_id );
        const auto  kind      = ShaderReflection::ClassifyImage( type );
        const auto  described = ShaderReflection::DescribeImageType( type );

        if ( resource.name == "u_ShapeNoise" )
        {
            EXPECT_EQ( kind, ShaderReflection::ImageKind::Image3D );
            EXPECT_EQ( described, "sampler3D" );
        }
        else if ( resource.name == "u_Radiance" )
        {
            EXPECT_EQ( kind, ShaderReflection::ImageKind::ImageCube );
            EXPECT_EQ( described, "samplerCube" );
        }
        else
        {
            EXPECT_EQ( kind, ShaderReflection::ImageKind::Image2D );
            EXPECT_EQ( described, "sampler2D" );
        }
    }

    for ( const auto& resource : resources.storage_images )
    {
        const auto& type = compiler.get_type( resource.base_type_id );
        EXPECT_EQ( ShaderReflection::DescribeImageType( type ),
                   resource.name == "u_OutVolume" ? "image3D" : "image2D" );
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
