#pragma once

#include <Common/Core/Core.hpp>
#include <Common/Core/ResultStr.hpp>

#include "ShaderReflectionTypes.hpp"

namespace Desert::ShaderResources
{
    class BaseBuffer
    {
    public:
        virtual ~BaseBuffer() = default;

        virtual void SetData( const void* data, uint32_t size, uint32_t offset = 0 ) = 0;

        // THIS USED TO BE `uint8_t* MapMemory()` PAIRED WITH `void UnmapMemory()`, AND THE PAIR WAS A
        // POINTER NOBODY EVER READ. Both call sites in the engine bound the return value to a
        // `[[maybe_unused]]` local and threw it away; what they actually wanted was the SIDE EFFECT —
        // "make sure there is somewhere to write before I start writing". Handing back a raw pointer to
        // express that was a standing invitation to `memcpy` through it (which is exactly what happened
        // ten times one layer down — see Engine/Graphic/MappedMemory.hpp), and the unmap half was a
        // documented no-op in every implementation, because these buffers are persistently mapped.
        //
        // So the question is asked in the form it was always being used in, and it ANSWERS: a caller
        // that cannot write must not go on to mark its data uploaded. That was the silent half of the
        // defect — a failed mapping made UpdateFields write nothing and then report the buffer clean.
        NO_DISCARD virtual Common::BoolResultStr EnsureMapped() = 0;

        virtual uint32_t GetBinding() const = 0;
        virtual uint32_t GetSize() const    = 0;

        virtual const std::vector<ShaderLayout::ShaderFieldLayout>& GetFields() const = 0;

        virtual const void* GetData() const = 0;
    };

} // namespace Desert::ShaderResources