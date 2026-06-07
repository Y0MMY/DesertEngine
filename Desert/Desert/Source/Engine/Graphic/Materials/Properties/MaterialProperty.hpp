#pragma once

#include <Engine/Graphic/Materials/MaterialBackend.hpp>

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
        void MarkClean()
        {
            if ( m_DirtyCount > 0 ) m_DirtyCount--;
        }

    protected:
        uint32_t m_DirtyCount = 3;
    };
} // namespace Desert::Graphic