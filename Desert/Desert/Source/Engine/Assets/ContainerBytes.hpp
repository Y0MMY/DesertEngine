#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Desert::Assets
{
    /**
     * @file
     * @brief The byte-level primitives every binary asset container in this engine is written with.
     *
     * WHY EXPLICIT LITTLE-ENDIAN AND NOT A memcpy OF A STRUCT. A container is a file that outlives the
     * machine that wrote it, and struct layout is a property of the compiler rather than of the format:
     * padding, alignment and endianness are all free to change under a `-O` flag, and the symptom of any
     * of them changing is an asset that reads as garbage on somebody else's build.
     *
     * WHY THIS IS A SHARED HEADER. These functions were written once for `.dcnv` and were about to be
     * written a second time for `.dcmv`. A CRC that exists twice is a CRC that can disagree with itself,
     * and the failure it would then produce — one container refusing a file the other accepts — is
     * exactly the class of defect this project keeps paying for. One implementation, two readers.
     */

    inline void WriteU32( std::vector<unsigned char>& out, uint32_t value )
    {
        out.push_back( static_cast<unsigned char>( value & 0xFFu ) );
        out.push_back( static_cast<unsigned char>( ( value >> 8 ) & 0xFFu ) );
        out.push_back( static_cast<unsigned char>( ( value >> 16 ) & 0xFFu ) );
        out.push_back( static_cast<unsigned char>( ( value >> 24 ) & 0xFFu ) );
    }

    inline void WriteU64( std::vector<unsigned char>& out, uint64_t value )
    {
        WriteU32( out, static_cast<uint32_t>( value & 0xFFFFFFFFu ) );
        WriteU32( out, static_cast<uint32_t>( ( value >> 32 ) & 0xFFFFFFFFu ) );
    }

    /// A float travels as its IEEE-754 bit pattern. `memcpy` rather than a union or a reinterpret_cast
    /// because it is the one spelling that is not a strict-aliasing violation.
    inline void WriteF32( std::vector<unsigned char>& out, float value )
    {
        uint32_t bits = 0u;
        std::memcpy( &bits, &value, sizeof( bits ) );
        WriteU32( out, bits );
    }

    inline uint32_t ReadU32( const unsigned char* at )
    {
        return static_cast<uint32_t>( at[0] ) | ( static_cast<uint32_t>( at[1] ) << 8 ) |
               ( static_cast<uint32_t>( at[2] ) << 16 ) | ( static_cast<uint32_t>( at[3] ) << 24 );
    }

    inline uint64_t ReadU64( const unsigned char* at )
    {
        return static_cast<uint64_t>( ReadU32( at ) ) | ( static_cast<uint64_t>( ReadU32( at + 4 ) ) << 32 );
    }

    inline float ReadF32( const unsigned char* at )
    {
        const uint32_t bits  = ReadU32( at );
        float          value = 0.0f;
        std::memcpy( &value, &bits, sizeof( value ) );
        return value;
    }

    /**
     * @brief CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320), table-free.
     *
     * WHY A CHECKSUM AT ALL, when the length is already checked. Length catches a truncated file, which
     * is the failure a half-finished write produces; it does not catch a file whose middle was
     * corrupted, and THAT failure renders as clouds with a wrong edge rather than as an error — the
     * most expensive shape of defect this programme has, because it looks like a tuning problem.
     */
    inline uint32_t Crc32( const unsigned char* data, size_t length )
    {
        uint32_t crc = 0xFFFFFFFFu;
        for ( size_t i = 0; i < length; ++i )
        {
            crc ^= static_cast<uint32_t>( data[i] );
            for ( int bit = 0; bit < 8; ++bit )
                crc = ( crc >> 1 ) ^ ( 0xEDB88320u & ( 0u - ( crc & 1u ) ) );
        }
        return ~crc;
    }
} // namespace Desert::Assets
