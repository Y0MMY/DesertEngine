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
        virtual ~MaterialProperty() = default;

        // THERE IS NO `Clone()`, AND THERE MUST NOT BE ONE THAT LOOKS LIKE THIS. It used to sit here as a
        // second pure virtual, implemented by all four property kinds, called from nowhere in the tree
        // (М9). Each body was a commented-out sketch over `return nullptr`, and every one of those
        // sketches was an ALIAS rather than a copy: the texture properties passed the SAME
        // `UniformImage2D`/`UniformImageCube` to the new object, so writing the "clone" would have written
        // the original's descriptor, and the buffer properties passed the same `UniformBuffer`, so the two
        // would have shared one GPU allocation and each claimed the other's FillKind route
        // (ShaderResources/BufferFillKind.hpp records what a mis-claimed route did to a frame). Two of the
        // four did not even compile: Texture2DProperty's called a `SetTexture` this class does not have,
        // and StorageBufferProperty's constructed a `UniformBufferProperty`.
        //
        // The operation a caller actually wants is `Material::CreateInstance()` — a MaterialInstance holds
        // its own overrides over a shared parent — and an editor working copy is
        // `Assets::SurfaceMaterialAsset::CreateWorkingCopy`, which duplicates the ASSET data and lets the
        // factory build fresh properties from it. Both exist and both are used; a per-property copy is on
        // neither path.
        virtual void Apply( MaterialBackend* backend ) = 0;

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
