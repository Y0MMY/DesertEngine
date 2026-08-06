#pragma once

#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/Materials/MaterialBackend.hpp>
#include <Engine/Graphic/Materials/Properties/PropertyDirty.hpp>

#include <array>

namespace Desert::Graphic
{
    class MaterialProperty
    {
    public:
        virtual ~MaterialProperty()                                                 = default;
        virtual void                              Apply( MaterialBackend* backend ) = 0;
        virtual std::unique_ptr<MaterialProperty> Clone() const                     = 0;

        // Dirty is tracked PER RENDERER SLOT. Per-frame GPU resources are stored per (frame x slot), and
        // a property is cleaned at most once per frame for whichever slot is recording — so one shared
        // counter drains on the first view and the second one never gets the value written into its own
        // copies. With a counter each, a view that starts recording later still owes itself the update.
        bool IsDirty() const
        {
            return m_DirtyCount[Slot()] > 0;
        }

        // Decrement at most once per rendered frame, and only for the slot that is recording. A material
        // can be bound many times in a single frame (one shared material drawing N objects) and
        // ApplyX()/Apply() both request a clean — without this guard the dirty window would drain far
        // faster than the frames-in-flight count, leaving some per-frame descriptor set still pointing at
        // the uninitialized dummy buffer.
        void MarkClean()
        {
            const uint32_t slot = Slot();
            if ( PropertyDirty::ConsumeCleanThisFrame( m_LastCleanFrame[slot] ) && m_DirtyCount[slot] > 0 )
                m_DirtyCount[slot]--;
        }

        // A write is owed to EVERY slot, not just the one that happens to be recording when it lands.
        void MarkDirty()
        {
            m_DirtyCount.fill( PropertyDirty::DirtyLifetime() );
        }

    protected:
        static uint32_t Slot()
        {
            const uint32_t slot = EngineContext::GetInstance().GetActiveRendererSlot();
            return slot < Engine::kMaxRendererSlots ? slot : 0;
        }

        // Stay dirty long enough to update every per-frame-in-flight descriptor set of every slot once.
        std::array<uint32_t, Engine::kMaxRendererSlots> m_DirtyCount     = MakeDirtyCounts();
        std::array<uint64_t, Engine::kMaxRendererSlots> m_LastCleanFrame = MakeLastCleaned();

    private:
        static std::array<uint32_t, Engine::kMaxRendererSlots> MakeDirtyCounts()
        {
            std::array<uint32_t, Engine::kMaxRendererSlots> counts{};
            counts.fill( PropertyDirty::DirtyLifetime() );
            return counts;
        }

        static std::array<uint64_t, Engine::kMaxRendererSlots> MakeLastCleaned()
        {
            std::array<uint64_t, Engine::kMaxRendererSlots> frames{};
            frames.fill( PropertyDirty::kNeverCleaned );
            return frames;
        }
    };
} // namespace Desert::Graphic
