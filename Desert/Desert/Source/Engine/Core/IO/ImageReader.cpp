#include <Engine/Core/IO/ImageReader.hpp>

#include <stb_image/stb_image.h>

namespace Desert::Core::IO
{

    bool ImageReader::IsHDR( const Common::Filepath& filepath )
    {
        return stbi_is_hdr( filepath.string().c_str() );
    }

    const ImageReader::ImageReaderHDRInfo ImageReader::ReadHDR( const Common::Filepath& filepath )
    {
        ImageReaderHDRInfo returnData;

        int    width, height, nrChannels;
        float* data = stbi_loadf( filepath.string().c_str(), &width, &height, &nrChannels, STBI_rgb_alpha );

        returnData.Width    = (uint32_t)width;
        returnData.Height   = (uint32_t)height;
        returnData.Channels = (uint32_t)nrChannels;

        uint32_t size = width * height * 16;
        returnData.Data.resize( size );

        memcpy( returnData.Data.data(), data, size );

        return returnData;
    }

    const ImageReader::ImageReaderInfo ImageReader::Read( const Common::Filepath& filepath, bool alpha )
    {
        ImageReaderInfo returnData;

        int            width, height, nrChannels;
        unsigned char* data = stbi_load( filepath.string().c_str(), &width, &height, &nrChannels,
                                         alpha ? STBI_rgb_alpha : STBI_rgb );

        returnData.Width    = (uint32_t)width;
        returnData.Height   = (uint32_t)height;
        returnData.Channels = (uint32_t)nrChannels;

        uint32_t size = width * height * 4;
        returnData.Data.resize(size);

        memcpy( returnData.Data.data(), data, size );

        return returnData;
    }

} // namespace Desert::Core::IO
