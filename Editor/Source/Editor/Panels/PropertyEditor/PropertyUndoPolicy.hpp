#pragma once

#include <Engine/Reflection/ReflectionTypes.hpp>

#include <cstddef>
#include <cstdint>

namespace Desert::Editor
{
    // WHICH REFLECTED PROPERTY EDITS REACH THE UNDO STACK, WHEN THEY ARE COMMITTED, AND HOW THEY ARE HELD.
    //
    // This decision used to be one boolean expression in the middle of PropertyEditorBuilder::DrawField.
    // That file draws ImGui and is compiled by no suite in the repository, so the expression was
    // unreachable by any test — and it was wrong in two opposite directions at once:
    //
    //   * `field.Type != FieldType::AssetHandle` EXCLUDED every asset slot. Assigning a sprite, an icon,
    //     a font or a video in Details was therefore irreversible: Ctrl+Z walked straight past it to the
    //     previous edit, and the reference the user had just replaced was gone with no way back.
    //   * every other type of that size was INCLUDED, and `std::string` is one of them. The entry the
    //     builder pushed is a byte copy of the field (CommandHistory::ByteCommand), so undoing a text
    //     edit memcpy'd the raw bytes of a std::string object — its heap pointer included — back over a
    //     live string whose buffer the edit had already freed. Seventeen reflected string fields could
    //     reach it.
    //
    // The two are the same defect seen from both sides: nothing said what "undoable" MEANS for a field
    // type, so every type got whatever the one expression happened to do to it. The two questions below
    // are the decomposition, and both are asserted per type in Desert/Tests/Editor/PropertyUndoPolicy.

    // WHEN the entry is pushed.
    enum class PropertyUndoKind : uint8_t
    {
        // Not recorded. Either the field cannot be edited, or it draws no widget of its own.
        None,

        // A widget the user HOLDS: drag, slider, colour picker, checkbox, text box. It reports `changed`
        // on every frame of the interaction, so one entry per frame would bury the stack. The "before"
        // state is captured when ImGui activates the item and pushed once when it deactivates after an
        // edit.
        Interactive,

        // A widget that commits in ONE click: an asset picker's Selectable, a drag-and-drop accept, a
        // Clear button, a combo entry. ImGui's activate/deactivate pair does not describe these — a
        // Selectable inside a popup is a different item from the row that owns the field, and
        // IsItemDeactivatedAfterEdit never fires for the field at all. That is precisely why the asset
        // slots were excluded rather than fixed. The entry is pushed the moment the value changes.
        Discrete
    };

    // HOW the before/after state is held in the entry.
    enum class PropertyUndoStorage : uint8_t
    {
        // Nothing is stored, because nothing is recorded.
        None,

        // A raw copy of the field's bytes, restored with memcpy. Valid only for a trivially copyable
        // field that owns no heap.
        Bytes,

        // The string's VALUE, restored by assignment. A std::string's representation is not its value:
        // copying its bytes copies a pointer the allocator may already have reclaimed.
        StringValue
    };

    // The undo kind for one reflected field.
    //
    // `size` is the field's own sizeof. The 64-byte ceiling is the byte entry's: it stores two copies of
    // the field per entry and the stack holds 256 of them, so a large POD block would be paid for on
    // every edit of it. Nothing reflected today is anywhere near it (the largest is a glm::vec4).
    [[nodiscard]] constexpr PropertyUndoKind PropertyUndoKindFor( Reflection::FieldType type, bool readOnly,
                                                                  std::size_t size ) noexcept
    {
        if ( readOnly || size == 0 || size > 64 )
            return PropertyUndoKind::None;

        switch ( type )
        {
            case Reflection::FieldType::Bool:
            case Reflection::FieldType::Int:
            case Reflection::FieldType::UInt:
            case Reflection::FieldType::Float:
            case Reflection::FieldType::Double:
            case Reflection::FieldType::Vec2:
            case Reflection::FieldType::Vec3:
            case Reflection::FieldType::Vec4:
            case Reflection::FieldType::String:
                return PropertyUndoKind::Interactive;

            case Reflection::FieldType::AssetHandle:
            case Reflection::FieldType::Enum:
                // Both commit from inside a popup: the asset pickers from a Selectable / drag-drop /
                // Clear button, the enum from a BeginCombo Selectable. Neither ever marks the FIELD's
                // own ImGui item as edited, so the interactive pair cannot see them.
                return PropertyUndoKind::Discrete;

            case Reflection::FieldType::Struct:
                // A struct row draws no widget of its own: DrawField recurses and each leaf below it
                // makes its own decision. Recording the parent as well would double every entry.
                return PropertyUndoKind::None;

            case Reflection::FieldType::Unknown:
                // The editor draws "(unsupported)" for it and no value can change.
                return PropertyUndoKind::None;
        }

        return PropertyUndoKind::None;
    }

    // How that entry holds its state. Answers None for exactly the fields the kind answers None for —
    // the suite asserts the two agree, because a recorded edit with nothing to restore is the shape the
    // string defect above had.
    [[nodiscard]] constexpr PropertyUndoStorage PropertyUndoStorageFor( Reflection::FieldType type, bool readOnly,
                                                                        std::size_t size ) noexcept
    {
        if ( PropertyUndoKindFor( type, readOnly, size ) == PropertyUndoKind::None )
            return PropertyUndoStorage::None;

        return type == Reflection::FieldType::String ? PropertyUndoStorage::StringValue
                                                     : PropertyUndoStorage::Bytes;
    }

    // What ImGui reported about this row THIS FRAME, lifted out of the draw code so the decision below
    // can be exercised without an ImGui context. The three are read straight off IsItemActivated(),
    // IsItemDeactivatedAfterEdit() and whatever the widget's own return value was.
    struct PropertyEditSignals
    {
        // The row's own item was released after an edit — the commit point of a held widget.
        bool DeactivatedAfterEdit = false;

        // The widget wrote a new value into the field this frame. For a held widget this is true on
        // every frame of the drag, which is exactly why it cannot be the commit point for one.
        bool Changed = false;

        // The field's bytes actually differ from what they held before the row was drawn. Re-picking
        // the value that was already there is not an edit.
        bool ValueDiffers = false;
    };

    // The entry to push at the end of a property row, if any.
    enum class PropertyUndoAction : uint8_t
    {
        Nothing,
        PushBytes,
        PushString
    };

    // THE COMMIT DECISION, and the reason it is a function rather than two ifs inside the draw code:
    // PropertyEditorBuilder.cpp is compiled by no suite, so for as long as this lived there the answer
    // for an asset slot ("push nothing, ever") could not be observed by anything.
    [[nodiscard]] constexpr PropertyUndoAction PropertyUndoActionFor( PropertyUndoKind           kind,
                                                                      PropertyUndoStorage        storage,
                                                                      const PropertyEditSignals& signals ) noexcept
    {
        const PropertyUndoAction push = storage == PropertyUndoStorage::StringValue
                                             ? PropertyUndoAction::PushString
                                             : PropertyUndoAction::PushBytes;

        switch ( kind )
        {
            case PropertyUndoKind::Interactive:
                // One entry for the whole interaction, at its end. `Changed` is deliberately NOT
                // consulted: a drag that ends back where it started still deactivates after an edit,
                // and ImGui is the authority on whether the item was edited at all.
                return signals.DeactivatedAfterEdit ? push : PropertyUndoAction::Nothing;

            case PropertyUndoKind::Discrete:
                // There is no deactivation to wait for — the value was written from inside a popup, by
                // a widget that is not this row's item. `ValueDiffers` is what keeps a re-pick of the
                // current value from putting a no-op step on the stack.
                return ( signals.Changed && signals.ValueDiffers ) ? push : PropertyUndoAction::Nothing;

            case PropertyUndoKind::None:
                return PropertyUndoAction::Nothing;
        }

        return PropertyUndoAction::Nothing;
    }
} // namespace Desert::Editor
