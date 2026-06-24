#pragma once

#include <string_view>
#include <cstring>

namespace Desert::Graphic
{
    enum class PropertyKind
    {
        Value,      // POD scalar/vector — can be written to a FieldProperty via SetRawBytes
        Texture2D,  // void* image pointer — written to a Texture2DProperty
    };

    // Runtime type tag so the editor can dispatch to the correct ImGui widget without RTTI.
    enum class PropertyTypeTag
    {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Bool,
        Int,
        Texture2D,
        Unknown,
    };

    // Per-property editor hints.  All fields have defaults so only relevant ones need to be set.
    struct PropertyEditorMeta
    {
        const char* displayName = nullptr;  // null = use IProperty::GetName()
        const char* category    = nullptr;  // null = no category grouping
        float       minVal      = 0.0f;
        float       maxVal      = 1.0f;
        float       step        = 0.01f;
        bool        isColor     = false;    // render as ColorEdit instead of DragFloat
        bool        isHidden    = false;    // skip in editor
        bool        isReadOnly  = false;    // show but disable editing
    };

    class IProperty
    {
    public:
        virtual ~IProperty() = default;

        virtual std::string_view GetName()       const = 0;
        virtual std::string_view GetShaderName() const = 0;
        virtual bool             IsDirty()       const = 0;
        virtual void             MarkClean()           = 0;
        virtual void             Reset()               = 0;

        virtual PropertyKind    GetKind()                    const = 0;
        virtual PropertyTypeTag GetTypeTag()                 const = 0;
        virtual size_t          GetByteSize()                const = 0;
        virtual void            CopyValueTo( void* out )     const = 0;

        virtual const PropertyEditorMeta& GetEditorMeta()  const                     = 0;
        virtual void                      SetEditorMeta( const PropertyEditorMeta& ) = 0;
    };

    // Base interface for objects that own typed properties (i.e. Material and its subclasses).
    // Kept separate from IProperty so TProperty.hpp has no dependency on Material headers.
    class IPropertyOwner
    {
    public:
        virtual void RegisterProperty( IProperty* prop ) = 0;

    protected:
        ~IPropertyOwner() = default;
    };

    // Strongly-typed material property. Set() is no-op when value hasn't changed.
    template <typename T>
    class TProperty final : public IProperty
    {
    public:
        TProperty( std::string_view name, std::string_view shaderName, T defaultValue )
             : m_Name( name )
             , m_ShaderName( shaderName )
             , m_Value( defaultValue )
             , m_Default( defaultValue )
        {
        }

        std::string_view GetName()       const override { return m_Name; }
        std::string_view GetShaderName() const override { return m_ShaderName; }
        bool             IsDirty()       const override { return m_Dirty; }
        void             MarkClean()           override { m_Dirty = false; }
        PropertyKind     GetKind()       const override { return PropertyKind::Value; }
        size_t           GetByteSize()   const override { return sizeof( T ); }

        PropertyTypeTag GetTypeTag() const override
        {
            if constexpr ( std::is_same_v<T, float> )          return PropertyTypeTag::Float;
            else if constexpr ( std::is_same_v<T, glm::vec2> ) return PropertyTypeTag::Vec2;
            else if constexpr ( std::is_same_v<T, glm::vec3> ) return PropertyTypeTag::Vec3;
            else if constexpr ( std::is_same_v<T, glm::vec4> ) return PropertyTypeTag::Vec4;
            else if constexpr ( std::is_same_v<T, bool> )      return PropertyTypeTag::Bool;
            else if constexpr ( std::is_same_v<T, int> )       return PropertyTypeTag::Int;
            else                                                return PropertyTypeTag::Unknown;
        }

        const PropertyEditorMeta& GetEditorMeta() const override { return m_Meta; }
        void SetEditorMeta( const PropertyEditorMeta& meta ) override { m_Meta = meta; }

        void CopyValueTo( void* out ) const override
        {
            memcpy( out, &m_Value, sizeof( T ) );
        }

        void Reset() override
        {
            if ( m_Value != m_Default )
            {
                m_Value = m_Default;
                m_Dirty = true;
            }
        }

        const T& Get()        const { return m_Value; }
        const T& GetDefault() const { return m_Default; }

        void Set( const T& value )
        {
            if ( m_Value != value )
            {
                m_Value = value;
                m_Dirty = true;
            }
        }

        void ForceSet( const T& value )
        {
            m_Value = value;
            m_Dirty = true;
        }

    private:
        std::string_view    m_Name;
        std::string_view    m_ShaderName;
        T                   m_Value;
        T                   m_Default;
        PropertyEditorMeta  m_Meta;
        // Start dirty so the default value is uploaded to the GPU on the first frame even when
        // Set() is called with the same value (equality check would skip it).
        bool                m_Dirty = true;
    };

    // Texture property — always marks dirty on Set (no equality check for pointers is meaningful here).
    class TTextureProperty final : public IProperty
    {
    public:
        TTextureProperty( std::string_view name, std::string_view shaderName )
             : m_Name( name )
             , m_ShaderName( shaderName )
        {
        }

        std::string_view GetName()       const override { return m_Name; }
        std::string_view GetShaderName() const override { return m_ShaderName; }
        bool             IsDirty()       const override { return m_Dirty; }
        void             MarkClean()           override { m_Dirty = false; }
        PropertyKind     GetKind()       const override { return PropertyKind::Texture2D; }
        PropertyTypeTag  GetTypeTag()    const override { return PropertyTypeTag::Texture2D; }
        size_t           GetByteSize()   const override { return sizeof( void* ); }

        const PropertyEditorMeta& GetEditorMeta() const override { return m_Meta; }
        void SetEditorMeta( const PropertyEditorMeta& meta ) override { m_Meta = meta; }

        void CopyValueTo( void* out ) const override
        {
            memcpy( out, &m_Value, sizeof( void* ) );
        }

        void Reset() override
        {
            m_Value = nullptr;
            m_Dirty = true;
        }

        void* Get()          const { return m_Value; }
        void  Set( void* v )       { m_Value = v; m_Dirty = true; }

    private:
        std::string_view    m_Name;
        std::string_view    m_ShaderName;
        PropertyEditorMeta  m_Meta;
        void*               m_Value = nullptr;
        bool                m_Dirty = false;
    };

} // namespace Desert::Graphic

// ---------------------------------------------------------------------------
// Declares a typed property member, auto-registers it with the owning Material,
// and generates Get##VarName / Set##VarName accessors.
//
// Usage (inside a Material subclass body):
//   MPROPERTY( float, Metallic, "u_Metallic", 0.0f )
// ---------------------------------------------------------------------------
// clang-format off
#define MPROPERTY( Type, VarName, ShaderName, Default )                                    \
    Desert::Graphic::TProperty<Type> VarName { #VarName, ShaderName, Default };            \
    struct VarName##_PropertyRegistrar                                                      \
    {                                                                                       \
        VarName##_PropertyRegistrar( Desert::Graphic::IPropertyOwner* owner,               \
                                     Desert::Graphic::IProperty*       prop )              \
        {                                                                                   \
            owner->RegisterProperty( prop );                                               \
        }                                                                                   \
    } VarName##_reg { this, &VarName };                                                    \
    Type Get##VarName() const { return VarName.Get(); }                                    \
    void Set##VarName( const Type& val ) { VarName.Set( val ); }

#define MTEXTURE_PROPERTY( VarName, ShaderName )                                           \
    Desert::Graphic::TTextureProperty VarName { #VarName, ShaderName };                    \
    struct VarName##_PropertyRegistrar                                                      \
    {                                                                                       \
        VarName##_PropertyRegistrar( Desert::Graphic::IPropertyOwner* owner,               \
                                     Desert::Graphic::IProperty*       prop )              \
        {                                                                                   \
            owner->RegisterProperty( prop );                                               \
        }                                                                                   \
    } VarName##_reg { this, &VarName };                                                    \
    void* Get##VarName() const { return VarName.Get(); }                                   \
    void  Set##VarName( void* val ) { VarName.Set( val ); }
// clang-format on
