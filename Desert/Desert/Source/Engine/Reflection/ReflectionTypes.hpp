#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <rflcpp/rfl/Generic.hpp>

namespace Desert::Reflection
{
    // The category of a reflected field. Drives both editor widget selection and (later) automatic
    // shader upload. Kept free of glm/asset dependencies so the reflection core stays standalone.
    enum class FieldType : uint8_t
    {
        Unknown = 0,
        Bool,
        Int,
        UInt,
        Float,
        Double,
        String,
        Vec2,
        Vec3,
        Vec4,
        Enum,
        Struct,
        AssetHandle,
    };

    // Editor/codegen metadata extracted from PROPERTY(...) attributes.
    struct PropertyMetadata
    {
        std::string DisplayName; // PROPERTY(DisplayName("..."))  — empty → use field name
        std::string Category;    // PROPERTY(Category("..."))     — empty → "Default"
        std::string Tooltip;     // PROPERTY(Tooltip("..."))      — hover help on the field row
        std::string Header;      // PROPERTY(Header("..."))       — section label drawn above the field

        bool  HasRange = false;  // PROPERTY(Range(min,max))
        float RangeMin = 0.0f;
        float RangeMax = 0.0f;

        bool        IsColor   = false; // PROPERTY(Color)               → color editor
        bool        IsAsset   = false; // PROPERTY(Asset<TextureAsset>) → asset picker
        std::string AssetType;         // reflected asset type name (e.g. "TextureAsset")
        bool        Thumbnail = false; // PROPERTY(Thumbnail)
        bool        ReadOnly  = false; // PROPERTY(ReadOnly)
        bool        Hidden    = false; // PROPERTY(Hidden)
    };

    struct TypeInfo; // fwd

    struct EnumValue
    {
        std::string Name;
        int64_t     Value = 0;
    };

    struct FieldInfo
    {
        std::string      Name;          // C++ field name
        FieldType        Type = FieldType::Unknown;
        std::size_t      Offset = 0;    // offsetof within the owning type
        std::size_t      Size   = 0;    // sizeof the field
        std::string      TypeName;      // C++ type spelling (for struct/enum/asset resolution)
        PropertyMetadata Meta;

        // For FieldType::Struct — resolved lazily from the registry by TypeName.
        const TypeInfo*         StructType = nullptr;
        // For FieldType::Enum.
        std::vector<EnumValue>  EnumValues;

        // Containers (std::vector<...>). The byte-offset serializer can't iterate/resize a vector
        // generically (it needs the element type at compile time), so the codegen emits typed lambdas
        // here. When set, the serializer routes this field through them instead of the switch above.
        bool                                                    IsContainer = false;
        std::function<rfl::Generic( const void* /*field*/ )>    SerializeContainer;
        std::function<void( void* /*field*/, const rfl::Generic& )> DeserializeContainer;

        const std::string& DisplayName() const
        {
            return Meta.DisplayName.empty() ? Name : Meta.DisplayName;
        }
    };

    struct TypeInfo
    {
        std::string            Name;
        std::size_t            Size = 0;
        std::vector<FieldInfo> Fields;

        // Returns a pointer to a process-wide default-constructed instance of the type (member initializers
        // give it the "factory defaults"), or nullptr if the codegen didn't provide one. Used by the editor's
        // reset-to-default: a field's default value is `GetDefaultInstance() + field.Offset`. Set via
        // TypeBuilder::WithDefault<T>() from the generated reflection code.
        const void* ( *GetDefaultInstance )() = nullptr;
    };
} // namespace Desert::Reflection
