#pragma once

#include <cstddef>
#include <vector>

namespace Desert::Editor
{
    // Byte-level helpers for editing the SAME reflected field across several selected objects (the
    // multi-select Details panel). Reflection addresses a field by (offset, size) inside its owning
    // object; only trivially-copyable value fields are ever passed here, so a plain memcmp/memcpy is
    // the correct comparison/assignment. Pure (std-only) so it is unit-tested directly.

    // Do objects @p a and @p b hold a different value for the field at (offset, size)?
    bool FieldDiffers( const void* a, const void* b, std::size_t offset, std::size_t size );

    // Does any object in @p others differ from @p base for that field? (Drives the "(mixed)" marker.)
    bool AnyFieldDiffers( const void* base, const std::vector<void*>& others, std::size_t offset,
                          std::size_t size );

    // Copy the field value from @p src into every object in @p dst (applies an edit to the selection).
    void BroadcastField( const void* src, const std::vector<void*>& dst, std::size_t offset,
                         std::size_t size );
} // namespace Desert::Editor
