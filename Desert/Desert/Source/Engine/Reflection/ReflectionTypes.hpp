#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    };
} // namespace Desert::Reflection
