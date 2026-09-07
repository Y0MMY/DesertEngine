#pragma once

#include <Common/Core/Core.hpp>
#include <Common/Core/ResultStr.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace Desert::Graphic
{
    // A LIVE MAPPING OF DEVICE MEMORY, OR A NAMED REFUSAL — AND THERE IS NO WAY TO REACH THE BYTES
    // WITHOUT THE ANSWER ARRIVING WITH THEM.
    //
    // The defect this type exists to make unwritable: `VulkanAllocator::MapMemory` used to hand back a
    // `uint8_t*`, and sixteen call sites took it. One turned a null into a refusal. Ten went straight into
    // `memcpy( mapped, source, size )`. A `memcpy` through null is not an exception and not a failed
    // operation — it is memory corruption or an immediate, unexplained death of the process, and it is the
    // same class Ф1 closed for file reads ("a primitive that aborts kills every soft branch above it")
    // left open for GPU memory. Making the mapping SAY why it failed (which it now does) changed nothing
    // about that: the failure merely became loud before writing through null anyway.
    //
    // WHY A TYPE AND NOT THIRTEEN `if`s. Thirteen hand-written checks are thirteen places for a fourteenth
    // to be forgotten, and the fourteenth is written by somebody who never read this paragraph. The same
    // move as Ф3's typed file read: the OLD SHAPE MUST NOT COMPILE. There is no `Data()`, no `Get()`, no
    // conversion to `void*` and no `operator*` here — deliberately, and asserted by `MappedMemoryGuard`
    // rather than merely intended — so `memcpy( mapping, src, n )` is a compile error at every call site
    // that could ever be written, not a crash at the one that was.
    //
    // AND IT BUYS WHAT Ф3's RETURN TYPE COULD NOT. `Common::ResultStr<T>::GetValue()` on a FAILED result
    // returns `T{}`, which for a pointer is a null the caller cannot tell from a mapped one — so
    // `ResultStr<uint8_t*>` would have been the same defect one method call further down (that is task Ф4,
    // filed separately). The guarantee here does not rest on anybody's discipline with an unwrap, because
    // there is no unwrap.
    //
    // WHAT YOU DO INSTEAD is name the transfer: `Write`, `ReadInto`, `Fill`. Each answers
    // `Common::BoolResultStr` and each is NO_DISCARD, so a caller that ignores the answer is a warning in
    // a workspace that has built with `warnings "Extra"` since 2026-09-06.
    //
    // THE BOUNDS ARE CHECKED TOO, and that is a second defect closed on the way past. Every one of the ten
    // `memcpy` sites trusted a size computed somewhere else against a buffer allocated somewhere else —
    // `VulkanVertexBuffer::SetData` wrote `dst + offset` for a caller-supplied offset with no bound of any
    // kind. A write that runs off the end of a mapping corrupts whatever VMA placed after it, with no
    // symptom at the site. Here it is a named refusal carrying both numbers.
    //
    // RAII, so the unmap cannot be forgotten or skipped by an early return: the mapping unmaps itself when
    // it goes out of scope, and the thirteen hand-paired `UnmapMemory` calls are gone with the pointer they
    // guarded. Move-only, because two owners would unmap twice.
    //
    // NO VULKAN IN THIS HEADER, ON PURPOSE. It sits beside `DeviceLost.hpp` for the same reason that one
    // does: the contract is about the engine, the implementation happens to be Vulkan's. The unmap is a
    // function pointer supplied by whoever made the mapping, so `Engine/Graphic` stays free of VMA and the
    // suite that proves the guarantees needs neither a device nor a driver to run.
    class MappedMemory
    {
    public:
        /// How the owner of the memory releases it. Receives the opaque allocation handle given to Live().
        using UnmapFn = void ( * )( void* allocation );

        /// An unmapped mapping that refuses everything. The default state is a refusal rather than an
        /// empty success: §1.4 of the contract — "not ready" and "nothing to do" must not be one value.
        MappedMemory() = default;

        /// The mapping did not happen, and this is why. @p reason is what the caller will print.
        static MappedMemory Refused( std::string reason )
        {
            MappedMemory refused;
            if ( !reason.empty() )
            {
                refused.m_Refusal = std::move( reason );
                refused.m_Reason  = nullptr; // the specific reason wins over the generic one
            }
            return refused;
        }

        /// A live mapping of @p size bytes at @p bytes, released through @p unmap( @p allocation ).
        static MappedMemory Live( void* allocation, uint8_t* bytes, std::size_t size, UnmapFn unmap )
        {
            if ( bytes == nullptr )
                return Refused( "the mapping reported success and handed back no address" );

            MappedMemory live;
            live.m_Allocation = allocation;
            live.m_Bytes      = bytes;
            live.m_Size       = size;
            live.m_Unmap      = unmap;
            live.m_Reason     = nullptr;
            return live;
        }

        ~MappedMemory()
        {
            Unmap();
        }

        MappedMemory( MappedMemory&& other ) noexcept
        {
            Adopt( std::move( other ) );
        }

        MappedMemory& operator=( MappedMemory&& other ) noexcept
        {
            if ( this != &other )
            {
                Unmap();
                Adopt( std::move( other ) );
            }
            return *this;
        }

        MappedMemory( const MappedMemory& )            = delete;
        MappedMemory& operator=( const MappedMemory& ) = delete;

        bool IsMapped() const noexcept
        {
            return m_Bytes != nullptr;
        }

        /// Explicit on purpose. An implicit one would let `memcpy( mapping, ... )` find a conversion.
        explicit operator bool() const noexcept
        {
            return IsMapped();
        }

        std::size_t GetSize() const noexcept
        {
            return m_Size;
        }

        /// Why there is nothing to write to. Empty while the mapping is live.
        ///
        /// By value, and the reason is worth a line: a mapping is created on the hot upload path (every
        /// dynamic vertex and index buffer, every frame), so the SUCCESS path must not allocate. Holding
        /// the three standing reasons as `const char*` and only the formatted one as a `std::string`
        /// keeps a live mapping allocation-free from construction to destruction — the first draft
        /// default-initialised a 35-character `std::string`, which is one malloc/free per upload for a
        /// sentence nobody was ever going to read.
        std::string GetRefusal() const
        {
            if ( !m_Refusal.empty() )
                return m_Refusal;
            return m_Reason != nullptr ? std::string( m_Reason ) : std::string();
        }

        /// @p bytes from @p source into the mapping at @p offset. Refuses, writing nothing, when the
        /// mapping is not live or the range does not fit.
        NO_DISCARD Common::BoolResultStr Write( const void* source, std::size_t bytes, std::size_t offset = 0 )
        {
            if ( source == nullptr && bytes != 0 )
                return Common::MakeError<bool>( "a mapped write was given a null source" );

            const auto room = Admits( bytes, offset, "write" );
            if ( !room.IsSuccess() )
                return room;

            if ( bytes != 0 )
                std::memcpy( m_Bytes + offset, source, bytes );
            return Common::MakeSuccess( true );
        }

        /// @p bytes out of the mapping at @p offset into @p destination. The readback direction — the
        /// unchecked form of this one dereferenced null as the SOURCE, which reads as a segfault in
        /// `memcpy` with no line of ours anywhere near it.
        NO_DISCARD Common::BoolResultStr ReadInto( void* destination, std::size_t bytes,
                                                   std::size_t offset = 0 ) const
        {
            if ( destination == nullptr && bytes != 0 )
                return Common::MakeError<bool>( "a mapped read was given a null destination" );

            const auto room = Admits( bytes, offset, "read" );
            if ( !room.IsSuccess() )
                return room;

            if ( bytes != 0 )
                std::memcpy( destination, m_Bytes + offset, bytes );
            return Common::MakeSuccess( true );
        }

        /// @p bytes of @p value into the mapping at @p offset.
        NO_DISCARD Common::BoolResultStr Fill( uint8_t value, std::size_t bytes, std::size_t offset = 0 )
        {
            const auto room = Admits( bytes, offset, "fill" );
            if ( !room.IsSuccess() )
                return room;

            if ( bytes != 0 )
                std::memset( m_Bytes + offset, value, bytes );
            return Common::MakeSuccess( true );
        }

        /// Release early. Idempotent, and the destructor's own path — a mapping that has been unmapped
        /// refuses every transfer afterwards rather than writing into memory the driver has taken back.
        void Unmap() noexcept
        {
            // A mapping that was never live keeps the reason it already had — "the map failed because X"
            // is more use to the next reader than "released".
            const bool wasLive = m_Bytes != nullptr;
            if ( wasLive && m_Unmap != nullptr )
                m_Unmap( m_Allocation );
            m_Allocation = nullptr;
            m_Bytes      = nullptr;
            m_Size       = 0;
            m_Unmap      = nullptr;
            if ( wasLive )
            {
                m_Refusal.clear();
                m_Reason = kReleased;
            }
        }

    private:
        static constexpr const char* kNeverAttempted = "the mapping was never attempted";
        static constexpr const char* kReleased       = "the mapping has already been released";
        static constexpr const char* kMovedOut       = "the mapping was moved out of this object";

        void Adopt( MappedMemory&& other ) noexcept
        {
            m_Allocation = other.m_Allocation;
            m_Bytes      = other.m_Bytes;
            m_Size       = other.m_Size;
            m_Unmap      = other.m_Unmap;
            m_Reason     = other.m_Reason;
            m_Refusal    = std::move( other.m_Refusal );

            other.m_Allocation = nullptr;
            other.m_Bytes      = nullptr;
            other.m_Size       = 0;
            other.m_Unmap      = nullptr;
            other.m_Reason     = kMovedOut;
            other.m_Refusal.clear();
        }

        // The one place the two questions are asked, so a new transfer cannot answer only one of them.
        // `offset > m_Size` is tested BEFORE `m_Size - offset` for the reason the subtraction is unsigned:
        // the other order wraps and admits the write it was meant to refuse.
        Common::BoolResultStr Admits( std::size_t bytes, std::size_t offset, const char* what ) const
        {
            if ( !IsMapped() )
                return Common::MakeFormattedError<bool>( "a mapped {} of {} byte(s) was refused: {}", what, bytes,
                                                         GetRefusal() );
            if ( offset > m_Size || bytes > m_Size - offset )
                return Common::MakeFormattedError<bool>(
                     "a mapped {} of {} byte(s) at offset {} does not fit a {}-byte mapping", what, bytes, offset,
                     m_Size );
            return Common::MakeSuccess( true );
        }

        void*       m_Allocation = nullptr;
        uint8_t*    m_Bytes      = nullptr;
        std::size_t m_Size       = 0;
        UnmapFn     m_Unmap      = nullptr;
        // The standing reason, or null while the mapping is live. A pointer rather than a string so that
        // a successful map costs no allocation at all (see GetRefusal).
        const char* m_Reason = kNeverAttempted;
        // The reason that had to be built — the driver's own words. Empty unless one was given.
        std::string m_Refusal;
    };
} // namespace Desert::Graphic
