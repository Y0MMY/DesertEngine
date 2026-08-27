#pragma once

#include <Engine/Core/FrameManager.hpp>

#include <bit>
#include <cstdint>

namespace Desert::Engine
{
    // Which renderer slots are taken RIGHT NOW. A slot is where per-(frame x view) GPU state lives
    // (Docs/RENDERER_FRAME_STATE.md), so it is owed only to views that exist: the editor creates and
    // destroys renderers freely — a scene view is opened and closed, a preview comes and goes — and a
    // counter that only ever went up ran out after five of those, after which every renderer folded onto
    // slot 0 and shared the main viewport's camera. That reads as "the preview moves when I move the
    // scene camera", and it is a picture defect with no error message at all.
    //
    // WHY THIS IS A SEPARATE, PURE, HEADER-ONLY CLASS. The accounting used to be three file-static
    // functions inside SceneRenderer.cpp, which no test suite compiles and none ever will — the file
    // needs a Vulkan device. The defect this guards against is a LEASE that is never returned, which is
    // invisible to a green sweep unless the bookkeeping can be driven directly, so the bookkeeping is
    // separated from the renderer that uses it. Nothing here touches Vulkan, the engine context, a log or
    // any global.
    class RendererSlotPool
    {
    public:
        // Returned by Claim() when every slot is taken. Deliberately NOT slot 0: the caller decides what
        // to do about an overflow (SceneRenderer records into slot 0 anyway and warns), and returning a
        // real-looking slot is what created the bug this sentinel exists to prevent — see Release().
        static constexpr uint32_t kNoFreeSlot = ~uint32_t( 0 );

        // Takes the lowest free slot, or kNoFreeSlot if there is none.
        [[nodiscard]] uint32_t Claim() noexcept
        {
            for ( uint32_t slot = 0; slot < kMaxRendererSlots; ++slot )
            {
                const uint32_t bit = 1u << slot;
                if ( ( m_InUse & bit ) == 0 )
                {
                    m_InUse |= bit;
                    return slot;
                }
            }
            return kNoFreeSlot;
        }

        // Gives a slot back. Only a value that came from a successful Claim() may be passed.
        //
        // kNoFreeSlot is ignored ON PURPOSE, and it is the whole reason the sentinel exists: an
        // overflowing renderer records into slot 0 without ever having leased it, so if its destructor
        // released "slot 0" it would hand away the MAIN VIEWPORT's lease. The next renderer created would
        // then be given slot 0 as free while the viewport was still using it — two live views writing one
        // slot, with the mask claiming only one was taken. The overflow path is the rare one, so this was
        // a corruption that only appeared after the seventh renderer of a session.
        void Release( uint32_t slot ) noexcept
        {
            if ( slot < kMaxRendererSlots )
                m_InUse &= ~( 1u << slot );
        }

        [[nodiscard]] bool IsInUse( uint32_t slot ) const noexcept
        {
            return slot < kMaxRendererSlots && ( m_InUse & ( 1u << slot ) ) != 0;
        }

        // How many views are alive. The mask IS the answer — a separate counter would be a second source
        // of truth for one fact, and the two would disagree the first time a renderer overflowed.
        [[nodiscard]] uint32_t InUseCount() const noexcept
        {
            return static_cast<uint32_t>( std::popcount( m_InUse ) );
        }

        [[nodiscard]] bool IsFull() const noexcept
        {
            return InUseCount() == kMaxRendererSlots;
        }

    private:
        uint32_t m_InUse = 0; // bit i = slot i taken
    };

    // One view's lease on a slot, held for exactly as long as the view exists.
    //
    // RAII rather than a claim/release pair written out at both ends, because the whole defect class here
    // is an end that never runs: a preview surface that is opened and closed without giving its slot back
    // silently costs one of six, and the sixth loss shows up as two panels sharing a camera. Making the
    // release a destructor is what lets the owner (SceneRenderer) be correct by construction, and lets a
    // GPU-less test drive the real type instead of a copy of its rules that could drift from it.
    class RendererSlotLease
    {
    public:
        explicit RendererSlotLease( RendererSlotPool& pool ) noexcept : m_Pool( &pool ), m_Slot( pool.Claim() )
        {
        }

        ~RendererSlotLease() noexcept
        {
            m_Pool->Release( m_Slot ); // a no-op when the pool was full; see RendererSlotPool::Release
        }

        // A lease is one view's identity for as long as it lives. Copying would hand two views one slot,
        // and moving would leave a released slot behind that something still records into.
        RendererSlotLease( const RendererSlotLease& )            = delete;
        RendererSlotLease& operator=( const RendererSlotLease& ) = delete;

        // False when every slot was taken. The view still renders — it just shares slot 0 and trades
        // per-frame state with whoever holds it, which the caller is expected to warn about.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_Slot != RendererSlotPool::kNoFreeSlot;
        }

        // Where this view RECORDS: its own slot, or slot 0 when it never got one. Distinct from the lease
        // itself, which stays kNoFreeSlot so the destructor cannot give away slot 0's lease.
        [[nodiscard]] uint32_t RecordingSlot() const noexcept
        {
            return IsValid() ? m_Slot : 0;
        }

    private:
        RendererSlotPool* m_Pool;
        uint32_t          m_Slot;
    };
} // namespace Desert::Engine
