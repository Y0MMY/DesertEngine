#pragma once

namespace Desert::Core::IO
{
    class ImageReader
    {
    public:
        struct ImageReaderHDRInfo
        {
            uint32_t           Width, Height, Channels;
            std::vector<float> Data;
        };

        struct ImageReaderInfo
        {
            uint32_t                   Width, Height, Channels;
            std::vector<unsigned char> Data;
        };

        static bool                     IsHDR( const Common::Filepath& filepath );
        static const ImageReaderHDRInfo ReadHDR( const Common::Filepath& filepath );
        static const ImageReaderInfo    Read( const Common::Filepath& filepath, bool alpha = true );
    };
} // namespace Desert::Core::IO