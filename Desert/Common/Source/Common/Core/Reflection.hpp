#pragma once

//  Desert Reflection — lightweight type-descriptor system.
//
//  Registration workflow (no code generation needed):
//    1. In the .hpp, annotate the struct with REFLECT() and PROPERTY(...) for documentation.
//       These macros are NO-OPS — they are future markers for DesertHeaderTool.
//    2. In the .cpp, implement GetTypeDescriptor() using TypeDescriptorBuilder.
//    3. The editor passes the object pointer + TypeDescriptor to PropertyEditorBuilder::Draw().
//       It reads/writes fields via getter/setter lambdas — NO hardcoded property names.
//
//  For GLM vec2/vec3/vec4 type deduction, include <Common/Core/ReflectionGlm.hpp> in the .cpp.

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <cstring>
#include <type_traits>
#include <cstddef>

namespace Desert::Reflect
{
    // -----------------------------------------------------------------------
    // FieldMeta — editor hints stored alongside a field
    // -----------------------------------------------------------------------
    struct FieldMeta
    {
        const char* displayName   = nullptr;  // nullptr → use field name
        const char* category      = nullptr;  // nullptr → "General"
        const char* shaderUniform = nullptr;  // non-null → also update MaterialInstance on change
        float       minVal        = 0.0f;
        float       maxVal        = 1.0f;
        float       step          = 0.01f;
        bool        isColor       = false;
        bool        isHidden      = false;
        bool        isReadOnly    = false;
    };

    // -----------------------------------------------------------------------
    // Attribute builders — passed as variadic args to TypeDescriptorBuilder::Field()
    // -----------------------------------------------------------------------
    inline auto DisplayName( const char* n )
    {
        return [n]( FieldMeta& m ) { m.displayName = n; };
    }
    inline auto Category( const char* c )
    {
        return [c]( FieldMeta& m ) { m.category = c; };
    }
    inline auto Range( float mn, float mx, float step = 0.01f )
    {
        return [mn, mx, step]( FieldMeta& m ) { m.minVal = mn; m.maxVal = mx; m.step = step; };
    }
    inline auto ShaderUniform( const char* uniformName )
    {
        return [uniformName]( FieldMeta& m ) { m.shaderUniform = uniformName; };
    }

    // Singleton-object style attributes — use without parentheses: ..., Color, ...
    struct _ColorAttr    { void operator()( FieldMeta& m ) const { m.isColor    = true; } };
    struct _HiddenAttr   { void operator()( FieldMeta& m ) const { m.isHidden   = true; } };
    struct _ReadOnlyAttr { void operator()( FieldMeta& m ) const { m.isReadOnly = true; } };

    inline constexpr _ColorAttr    Color;
    inline constexpr _HiddenAttr   Hidden;
    inline constexpr _ReadOnlyAttr ReadOnly;

    template <typename... Mods>
    FieldMeta BuildFieldMeta( Mods&&... mods )
    {
        FieldMeta m;
        ( mods( m ), ... );
        return m;
    }

    // -----------------------------------------------------------------------
    // FieldTypeTag — runtime type id used by the editor to pick the right widget
    // -----------------------------------------------------------------------
    enum class FieldTypeTag
    {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Int,
        Bool,
        String,
        Unknown,
    };

    // Default deduction — handles float/int/bool/string.
    // GLM specialisations live in ReflectionGlm.hpp.
    template <typename T>
    constexpr FieldTypeTag DeduceTypeTag()
    {
        using D = std::decay_t<T>;
        if constexpr ( std::is_same_v<D, float> )            return FieldTypeTag::Float;
        else if constexpr ( std::is_same_v<D, int> )         return FieldTypeTag::Int;
        else if constexpr ( std::is_same_v<D, bool> )        return FieldTypeTag::Bool;
        else if constexpr ( std::is_same_v<D, std::string> ) return FieldTypeTag::String;
        else                                                   return FieldTypeTag::Unknown;
    }

    // -----------------------------------------------------------------------
    // FieldDescriptor — one reflected field
    // -----------------------------------------------------------------------
    struct FieldDescriptor
    {
        std::string  name;
        FieldMeta    meta;
        FieldTypeTag typeTag = FieldTypeTag::Unknown;
        size_t       size    = 0;

        // Raw read: copies field value into *outValue (outValue must have size bytes)
        std::function<void( const void* obj, void* outValue )> getter;

        // Raw write: copies *inValue into the field (inValue must have size bytes)
        std::function<void( void* obj, const void* inValue )> setter;

        const char* GetDisplayName() const
        {
            return meta.displayName ? meta.displayName : name.c_str();
        }

        const char* GetCategory() const
        {
            return meta.category ? meta.category : "General";
        }
    };

    // -----------------------------------------------------------------------
    // TypeDescriptor — all reflected fields for one type
    // -----------------------------------------------------------------------
    struct TypeDescriptor
    {
        std::string                  typeName;
        std::vector<FieldDescriptor> fields;

        const FieldDescriptor* FindField( std::string_view n ) const
        {
            for ( const auto& f : fields )
                if ( f.name == n )
                    return &f;
            return nullptr;
        }

        // Convenience: read a T-typed field via its getter.
        template <typename T>
        T GetValue( const void* obj, const FieldDescriptor& fd ) const
        {
            T val{};
            fd.getter( obj, &val );
            return val;
        }

        // Convenience: write a T-typed field via its setter.
        template <typename T>
        void SetValue( void* obj, const FieldDescriptor& fd, const T& val ) const
        {
            fd.setter( obj, &val );
        }
    };

    // -----------------------------------------------------------------------
    // TypeDescriptorBuilder<T> — fluent API; used in .cpp GetTypeDescriptor()
    //
    // Example:
    //   static const TypeDescriptor& MyStruct::GetTypeDescriptor()
    //   {
    //       using namespace Desert::Reflect;
    //       static TypeDescriptor s = TypeDescriptorBuilder<MyStruct>("MyStruct")
    //           .Field(&MyStruct::m_Albedo, "Albedo",
    //                  DisplayName("Albedo Color"), Category("Surface"), Color,
    //                  ShaderUniform("AlbedoColor"))
    //           .Field(&MyStruct::m_Metallic, "Metallic",
    //                  DisplayName("Metallic"), Category("Surface"),
    //                  Range(0.0f, 1.0f), ShaderUniform("MetallicValue"))
    //           .Build();
    //       return s;
    //   }
    // -----------------------------------------------------------------------
    template <typename T>
    class TypeDescriptorBuilder
    {
    public:
        explicit TypeDescriptorBuilder( const char* typeName )
        {
            m_Desc.typeName = typeName;
        }

        // Register a field via pointer-to-member.
        // Metadata attributes are passed after the field name.
        template <typename F, typename... Mods>
        TypeDescriptorBuilder& Field( F T::*memberPtr, const char* fieldName, Mods&&... mods )
        {
            FieldDescriptor fd;
            fd.name    = fieldName;
            fd.meta    = BuildFieldMeta( std::forward<Mods>( mods )... );
            fd.typeTag = DeduceFieldType( memberPtr );
            fd.size    = sizeof( F );

            fd.getter = [memberPtr]( const void* obj, void* out )
            {
                const F& val = static_cast<const T*>( obj )->*memberPtr;
                memcpy( out, &val, sizeof( F ) );
            };

            fd.setter = [memberPtr]( void* obj, const void* in )
            {
                F& val = static_cast<T*>( obj )->*memberPtr;
                memcpy( &val, in, sizeof( F ) );
            };

            m_Desc.fields.push_back( std::move( fd ) );
            return *this;
        }

        TypeDescriptor Build()
        {
            return std::move( m_Desc );
        }

    private:
        template <typename F>
        static constexpr FieldTypeTag DeduceFieldType( F T::* )
        {
            return DeduceTypeTag<std::decay_t<F>>();
        }

        TypeDescriptor m_Desc;
    };

} // namespace Desert::Reflect

// -----------------------------------------------------------------------
// Header-level annotation macros (NO-OPS — documentation only).
// Future DesertHeaderTool will parse these and generate GetTypeDescriptor() .cpp.
// -----------------------------------------------------------------------

// Mark a struct/class as reflectable.
#define REFLECT()

// Annotate the next field with editor metadata.
// Usage: PROPERTY( DisplayName("Albedo"), Category("Surface"), Color )
#define PROPERTY( ... )
