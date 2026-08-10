#include <Engine/Graphic/API/Vulkan/VulkanShaderReflection.hpp>

#include <format>

namespace Desert::Graphic::API::Vulkan::ShaderReflection
{
    namespace
    {
        // Folds this stage into a resource's stage mask. The entry is default-constructed on first
        // touch, so a resource declared by two stages ends up with both bits and one descriptor.
        template <typename TResource>
        void FillResource( TResource& entry, uint32_t binding, const std::string& name,
                           Core::Formats::ShaderStage stage )
        {
            entry.BindingPoint = binding;
            entry.Name         = name;
            entry.ShaderStage  = ( Core::Formats::ShaderStage )( (uint32_t)entry.ShaderStage | (uint32_t)stage );
        }
    } // namespace

    ImageKind ClassifyImage( const spirv_cross::SPIRType& type )
    {
        const auto& image = type.image;

        // Arrayed and multisampled images need a different view type and a different bind path, and
        // the engine builds neither. Approximating them by their base dimension is what the old name
        // heuristic effectively did — refuse instead.
        if ( image.ms || image.arrayed )
        {
            return ImageKind::Unsupported;
        }

        switch ( image.dim )
        {
            case spv::Dim2D:
                return ImageKind::Image2D;
            case spv::Dim3D:
                return ImageKind::Image3D;
            case spv::DimCube:
                return ImageKind::ImageCube;
            default:
                return ImageKind::Unsupported;
        }
    }

    std::string DescribeImageType( const spirv_cross::SPIRType& type )
    {
        const auto& image = type.image;

        // SPIR-V's `sampled`: 1 = read through a sampler, 2 = read/written as a storage image.
        std::string described = image.sampled == 2 ? "image" : "sampler";

        switch ( image.dim )
        {
            case spv::Dim1D:
                described += "1D";
                break;
            case spv::Dim2D:
                described += "2D";
                break;
            case spv::Dim3D:
                described += "3D";
                break;
            case spv::DimCube:
                described += "Cube";
                break;
            case spv::DimRect:
                described += "2DRect";
                break;
            case spv::DimBuffer:
                described += "Buffer";
                break;
            case spv::DimSubpassData:
                described += "SubpassData";
                break;
            default:
                described += std::format( "<dim {}>", (int)image.dim );
                break;
        }

        if ( image.ms )
        {
            described += "MS";
        }
        if ( image.arrayed )
        {
            described += "Array";
        }

        return described;
    }

    std::vector<std::string> ReflectStage( const std::vector<uint32_t>& spirv, Core::Formats::ShaderStage stage,
                                           ShaderResource::ReflectionData& data )
    {
        std::vector<std::string> diagnostics;

        spirv_cross::CompilerGLSL    compiler( spirv );
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        // Uniform Buffers
        for ( const auto& resource : resources.uniform_buffers )
        {
            uint32_t set     = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
            auto&    ub      = data.ShaderDescriptorSets[set].UniformBuffers[binding];
            FillResource( ub, binding, resource.name, stage );

            const auto& structType = compiler.get_type( resource.base_type_id );
            ub.Size                = (uint32_t)compiler.get_declared_struct_size( structType );

            // Populate fields once; multi-stage shaders call ReflectStage() per stage, avoid duplicates.
            if ( ub.Fields.empty() )
            {
                for ( uint32_t i = 0; i < (uint32_t)structType.member_types.size(); ++i )
                {
                    ShaderResources::ShaderLayout::ShaderFieldLayout field;
                    field.Name   = compiler.get_member_name( resource.base_type_id, i );
                    field.Offset = compiler.type_struct_member_offset( structType, i );
                    field.Size   = (uint32_t)compiler.get_declared_struct_member_size( structType, i );

                    const auto& memberType = compiler.get_type( structType.member_types[i] );
                    field.ArraySize        = memberType.array.empty() ? 1u : memberType.array[0];

                    ub.Fields.push_back( std::move( field ) );
                }
            }
        }

        // Sampled images — one bucket per view type, decided by the DECLARED type. The name is never
        // consulted: `u_EnvNoise` may well be a sampler2D and `u_Radiance` a samplerCube.
        for ( const auto& resource : resources.sampled_images )
        {
            uint32_t    set       = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t    binding   = compiler.get_decoration( resource.id, spv::DecorationBinding );
            const auto& imageType = compiler.get_type( resource.base_type_id );

            // An ARRAY of samplers (`sampler2D u_Foo[4]`) is a different thing from an arrayed image:
            // it needs descriptorCount > 1, and every layout this engine builds hardcodes 1.
            if ( !compiler.get_type( resource.type_id ).array.empty() )
            {
                diagnostics.push_back( std::format(
                     "resource '{}' (set {}, binding {}) is an array of {}; arrays of descriptors are "
                     "not supported — declare separate bindings",
                     resource.name, set, binding, DescribeImageType( imageType ) ) );
                continue;
            }

            switch ( ClassifyImage( imageType ) )
            {
                case ImageKind::Image2D:
                    FillResource( data.ShaderDescriptorSets[set].Image2DSamplers[binding], binding, resource.name,
                                  stage );
                    break;
                case ImageKind::Image3D:
                    FillResource( data.ShaderDescriptorSets[set].Image3DSamplers[binding], binding, resource.name,
                                  stage );
                    break;
                case ImageKind::ImageCube:
                    FillResource( data.ShaderDescriptorSets[set].ImageCubeSamplers[binding], binding,
                                  resource.name, stage );
                    break;
                case ImageKind::Unsupported:
                    diagnostics.push_back(
                         std::format( "sampled image '{}' (set {}, binding {}) is a {}, which the engine "
                                      "cannot bind",
                                      resource.name, set, binding, DescribeImageType( imageType ) ) );
                    break;
            }
        }

        // Storage Buffers
        for ( const auto& resource : resources.storage_buffers )
        {
            uint32_t set     = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
            auto&    sb      = data.ShaderDescriptorSets[set].StorageBuffers[binding];
            FillResource( sb, binding, resource.name, stage );
            auto& type = compiler.get_type( resource.base_type_id );
            sb.Size    = ( type.member_types.empty() || type.array.size() > 0 )
                              ? 0
                              : (uint32_t)compiler.get_declared_struct_size( type );
        }

        // Storage images (`writeonly image2D` / `imageCube` / `image3D` in compute shaders). 2D and
        // cube share a bucket on purpose: a storage binding is written from a mip VIEW the caller
        // supplies, which already carries its own view type, so nothing downstream needs the two
        // apart — and keeping them together is what leaves the existing IBL compute chain untouched
        // by this change. A volume is split off because the storage path binds 2D and cube only.
        for ( const auto& resource : resources.storage_images )
        {
            uint32_t    set       = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t    binding   = compiler.get_decoration( resource.id, spv::DecorationBinding );
            const auto& imageType = compiler.get_type( resource.base_type_id );

            if ( !compiler.get_type( resource.type_id ).array.empty() )
            {
                diagnostics.push_back( std::format(
                     "resource '{}' (set {}, binding {}) is an array of {}; arrays of descriptors are "
                     "not supported — declare separate bindings",
                     resource.name, set, binding, DescribeImageType( imageType ) ) );
                continue;
            }

            switch ( ClassifyImage( imageType ) )
            {
                case ImageKind::Image2D:
                case ImageKind::ImageCube:
                    FillResource( data.ShaderDescriptorSets[set].StorageImage2DSamplers[binding], binding,
                                  resource.name, stage );
                    break;
                case ImageKind::Image3D:
                    FillResource( data.ShaderDescriptorSets[set].StorageImage3DSamplers[binding], binding,
                                  resource.name, stage );
                    break;
                case ImageKind::Unsupported:
                    diagnostics.push_back(
                         std::format( "storage image '{}' (set {}, binding {}) is a {}, which the engine "
                                      "cannot bind",
                                      resource.name, set, binding, DescribeImageType( imageType ) ) );
                    break;
            }
        }

        // Push Constants
        if ( !resources.push_constant_buffers.empty() )
        {
            const auto&    res          = resources.push_constant_buffers[0];
            auto&          type         = compiler.get_type( res.base_type_id );
            const uint32_t declaredSize = (uint32_t)compiler.get_declared_struct_size( type );
            if ( !data.PushConstantRanges )
            {
                ShaderResources::ShaderLayout::PushConstantRange range;
                range.Offset            = 0;
                range.Size              = declaredSize;
                range.Name              = res.name;
                range.ShaderStage       = stage;
                data.PushConstantRanges = range;
            }
            else
            {
                data.PushConstantRanges->ShaderStage = ( Core::Formats::ShaderStage )(
                     (uint32_t)data.PushConstantRanges->ShaderStage | (uint32_t)stage );
                // Different stages may declare the same push-constant block but glslang strips members
                // a stage doesn't use, so each reports a different declared size. The pipeline-layout
                // range must span the largest, or a stage's access lands outside the range.
                if ( declaredSize > data.PushConstantRanges->Size )
                {
                    data.PushConstantRanges->Size = declaredSize;
                }
            }
        }

        return diagnostics;
    }

} // namespace Desert::Graphic::API::Vulkan::ShaderReflection
