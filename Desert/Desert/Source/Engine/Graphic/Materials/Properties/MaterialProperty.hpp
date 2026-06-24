#pragma once

#include <Engine/Graphic/Materials/MaterialBackend.hpp>
#include <Engine/Graphic/Materials/Properties/PropertyDirty.hpp>

namespace Desert::Graphic
{
    class MaterialProperty
    {
    public:
        virtual ~MaterialProperty()                                                 = default;
        virtual void                              Apply( MaterialBackend* backend ) = 0;
        virtual std::unique_ptr<MaterialProperty> Clone() const                     = 0;

        bool IsDirty() const
        {
            return m_DirtyCount > 0;
        }

        // Decrement at most once per rendered frame. A material can be bound many times in a single
        // frame (one shared material drawing N objects) and ApplyX()/Apply() both request a clean —
        // without this guard the dirty window would drain far faster than the frames-in-flight count,
        // leaving some per-frame descriptor set still pointing at the uninitialized dummy buffer.
        void MarkClean()
        {
            if ( PropertyDirty::ConsumeCleanThisFrame( m_LastCleanFrame ) && m_DirtyCount > 0 )
                m_DirtyCount--;
        }

        void MarkDirty()
        {
            m_DirtyCount = PropertyDirty::DirtyLifetime();
        }

    protected:
        // Stay dirty long enough to update every per-frame-in-flight descriptor set exactly once.
        uint32_t m_DirtyCount     = PropertyDirty::DirtyLifetime();
        uint64_t m_LastCleanFrame = PropertyDirty::kNeverCleaned;
    };
} // namespace Desert::Graphic