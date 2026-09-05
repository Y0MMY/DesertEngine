#include "PropertyReset.hpp"

#include <Editor/Core/CommandHistory.hpp>

#include <cstddef>
#include <cstring>

namespace Desert::Editor
{
    bool ResetFieldToDefault( void* object, const Reflection::FieldInfo& field, const void* defaultObject,
                              CommandHistory& history )
    {
        if ( !object || !defaultObject || field.Size == 0 )
            return false;

        void*       p = static_cast<std::byte*>( object ) + field.Offset;
        const void* d = static_cast<const std::byte*>( defaultObject ) + field.Offset;

        if ( std::memcmp( p, d, field.Size ) == 0 )
            return false; // already at the default — no edit happened, so none is recorded

        // Record BEFORE overwriting: Push copies both byte ranges, so `p` still holding the old value
        // is exactly the "old" side of the undo pair.
        history.Push( p, /*oldBytes*/ p, /*newBytes*/ d, field.Size );
        std::memcpy( p, d, field.Size );
        return true;
    }
} // namespace Desert::Editor
