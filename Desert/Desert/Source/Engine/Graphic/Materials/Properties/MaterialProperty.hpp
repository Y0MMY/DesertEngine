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
            return m_Dirty;
        }
        void MarkClean()
        {
            m_Dirty = false;
        }

    protected:
        bool m_Dirty = true;
    };
} // namespace Desert::Graphic