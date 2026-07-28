#include <Engine/Core/IO/ImageReader.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <stb_image/stb_image.h>

namespace Desert::Core::IO
{
    // All three entry points read the FILE BYTES through Common::Utils::FileSystem (disk first, then a
    // mounted .dpak) and hand them to the stb *_from_memory loaders — stb never touches paths itself,
    // so packaged games load texture pixels straight from the archive.

    bool ImageReader::IsHDR( const Common::Filepath& filepath )
    {
        if ( !Common::Utils::FileSystem::Exists( filepath ) )
            return false;
        const auto bytes = Common::Utils::FileSystem::ReadByteFileContent( filepath );
        return stbi_is_hdr_from_memory( bytes.data(), static_cast<int>( bytes.size() ) );
    }

    const ImageReader::ImageReaderHDRInfo ImageReader::ReadHDR( const Common::Filepath& filepath )
    {
        ImageReaderHDRInfo returnData;

        const auto bytes = Common::Utils::FileSystem::ReadByteFileContent( filepath );

        int    width, height, nrChannels;
        float* data = stbi_loadf_from_memory( bytes.data(), static_cast<int>( bytes.size() ), &width,
                                              &height, &nrChannels, STBI_rgb_alpha );
        if ( !data )
            return returnData; // Width/Height stay 0 -> caller-visible failure

        returnData.Width    = (uint32_t)width;
        returnData.Height   = (uint32_t)height;
        returnData.Channels = (uint32_t)nrChannels;

        uint32_t size = width * height * 16;
        returnData.Data.resize( size );

        memcpy( returnData.Data.data(), data, size );
        stbi_image_free( data );

        return returnData;
    }

    const ImageReader::ImageReaderInfo ImageReader::Read( const Common::Filepath& filepath, bool alpha )
    {
        ImageReaderInfo returnData;

        const auto bytes = Common::Utils::FileSystem::ReadByteFileContent( filepath );

        int            width, height, nrChannels;
        unsigned char* data = stbi_load_from_memory( bytes.data(), static_cast<int>( bytes.size() ), &width,
                                                     &height, &nrChannels, alpha ? STBI_rgb_alpha : STBI_rgb );
        if ( !data )
            return returnData; // Width/Height stay 0 -> caller-visible failure

        returnData.Width    = (uint32_t)width;
        returnData.Height   = (uint32_t)height;
        returnData.Channels = (uint32_t)nrChannels;
        uint32_t size = width * height * 4;
        returnData.Data.resize(size);

        memcpy( returnData.Data.data(), data, size );
        stbi_image_free( data );

        return returnData;
    }

} // namespace Desert::Core::IO
