#pragma once

namespace Desert::Graphic
{
    typedef unsigned int RenderingID;
    typedef unsigned int BindingPoint;

    enum class BufferUsage
    {
        None    = 0,
        Static  = 1,
        Dynamic = 2
    };

    enum class BindUsage
    {
        Bind   = 0,
        Unbind = 1
    };

} // namespace Desert::Graphic