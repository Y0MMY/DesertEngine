#pragma once

#include <Engine/Reflection/ReflectionTypes.hpp>

namespace Desert::Editor
{
    class CommandHistory;

    // Resets one reflected field of @p object to the value it holds in @p defaultObject (the type's
    // default-constructed instance, TypeInfo::GetDefaultInstance) and RECORDS the edit in @p history,
    // so the revision moves — which is what "unsaved changes", the save prompt, autosave and Ctrl+Z
    // all key on, engine-wide.
    //
    // Returns true when the field's bytes actually changed. The caller MUST consume that: the reset
    // used to change memory and report nothing, so the edit was invisible to undo, to the dirty star,
    // to autosave and to the multi-select broadcast (Д29 — "reset to default не работает"). A no-op
    // reset (field already at its default) returns false and records nothing: the revision moves
    // exactly when a value does.
    //
    // Byte-level on purpose, exactly like Editor/Core/MultiEdit.hpp: only trivially-copyable value
    // fields ever offer the reset button (see `resettable` in PropertyEditorBuilder::DrawField), so
    // memcmp/memcpy over [Offset, Offset+Size) is the whole operation.
    bool ResetFieldToDefault( void* object, const Reflection::FieldInfo& field, const void* defaultObject,
                              CommandHistory& history );
} // namespace Desert::Editor
