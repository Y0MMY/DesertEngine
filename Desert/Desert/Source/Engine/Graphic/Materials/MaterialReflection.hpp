#pragma once

#include <rflcpp/rfl.hpp>
#include <variant>
#include <unordered_map>
#include <vector>
#include <optional>

namespace rfl
{
    template <>
    struct Reflector<glm::vec3>
    {
        using ReflType = std::array<float, 3>;

        static inline glm::vec3 to( const ReflType& arr ) noexcept
        {
            return glm::vec3( arr[0], arr[1], arr[2] );
        }

        static inline ReflType from( const glm::vec3& v )
        {
            return { v.x, v.y, v.z };
        }
    };
} // namespace rfl

struct ColorAttribute
{
    bool srgb = true;
    bool hdr  = false;
};

struct DirectionAttribute
{
    bool normalized = true;
    bool allow_zero = false;
};

struct TextureAttribute
{
    std::string file_filter = "All Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga)";
    bool        sRGB        = true;
};

struct RangeAttribute
{
    float min_value;
    float max_value;
    float step = 0.01f;
};

using AttributeVariant = std::variant<ColorAttribute, DirectionAttribute, TextureAttribute, RangeAttribute, int,
                                      float, bool, std::string>;

using MetadataVariant = AttributeVariant;
using MetadataMap     = std::vector<std::pair<std::string, MetadataVariant>>;

template <rfl::internal::StringLiteral _name, typename T>
struct PropertiesImpl
{
    rfl::Field<_name, T>                    value;
    rfl::Field<"display_name", std::string> display_name;
    rfl::Field<"description", std::string>  description;
    rfl::Field<"category", std::string>     category = "General";
    rfl::Field<"editable", bool>            editable = true;
    rfl::Field<"visible", bool>             visible  = true;
    rfl::Field<"metadata", MetadataMap>     metadata;
};

template <rfl::internal::StringLiteral _name, typename T>
class Property
{
private:
    T                             value_;
    std::string                   display_name_;
    std::string                   description_;
    std::string                   category_ = "General";
    bool                          editable_ = true;
    bool                          visible_  = true;
    std::vector<AttributeVariant> attributes_;

public:
    Property( const T& val = {}, const std::string& disp_name = "" ) : value_( val ), display_name_( disp_name )
    {
    }

    Property( const T& val, const std::string& disp_name, const std::string& desc,
              const std::string& cat = "General", bool edit = true, bool vis = true )
         : value_( val ), display_name_( disp_name ), description_( desc ), category_( cat ), editable_( edit ),
           visible_( vis )
    {
    }

    ~Property() = default; 

    Property( const Property& other )
         : value_( other.value_ ), display_name_( other.display_name_ ), description_( other.description_ ),
           category_( other.category_ ), editable_( other.editable_ ), visible_( other.visible_ ),
           attributes_( other.attributes_ ) 
    {
    }

    Property& operator=( const Property& other )
    {
        if ( this != &other )
        {
            value_        = other.value_;
            display_name_ = other.display_name_;
            description_  = other.description_;
            category_     = other.category_;
            editable_     = other.editable_;
            visible_      = other.visible_;
            attributes_   = other.attributes_;
        }
        return *this;
    }

    Property( Property&& other ) noexcept
         : value_( std::move( other.value_ ) ), display_name_( std::move( other.display_name_ ) ),
           description_( std::move( other.description_ ) ), category_( std::move( other.category_ ) ),
           editable_( other.editable_ ), visible_( other.visible_ ), attributes_( std::move( other.attributes_ ) )
    {
    }

    Property& operator=( Property&& other ) noexcept
    {
        if ( this != &other )
        {
            value_        = std::move( other.value_ );
            display_name_ = std::move( other.display_name_ );
            description_  = std::move( other.description_ );
            category_     = std::move( other.category_ );
            editable_     = other.editable_;
            visible_      = other.visible_;
            attributes_   = std::move( other.attributes_ );
        }
        return *this;
    }

    Property& color( bool srgb = true, bool hdr = false )
    {
        attributes_.push_back( ColorAttribute{ srgb, hdr } );
        return *this;
    }

    Property& direction( bool normalized = true, bool allow_zero = false )
    {
        attributes_.push_back( DirectionAttribute{ normalized, allow_zero } );
        return *this;
    }

    Property& texture( const std::string& filter = "", bool srgb = true )
    {
        attributes_.push_back(
             TextureAttribute{ filter.empty() ? "All Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga)" : filter, srgb } );
        return *this;
    }

    Property& range( float min, float max, float step = 0.01f )
    {
        attributes_.push_back( RangeAttribute{ min, max, step } );
        return *this;
    }

    Property& description( const std::string& desc )
    {
        description_ = desc;
        return *this;
    }

    Property& category( const std::string& cat )
    {
        category_ = cat;
        return *this;
    }

    Property& editable( bool edit )
    {
        editable_ = edit;
        return *this;
    }

    Property& visible( bool vis )
    {
        visible_ = vis;
        return *this;
    }

    Property& attribute( auto&& data )
    {
        attributes_.push_back( std::forward<decltype( data )>( data ) );
        return *this;
    }

    operator T&()
    {
        return value_;
    }

    operator const T&() const
    {
        return value_;
    }

    Property& operator=( const T& new_value )
    {
        value_ = new_value;
        return *this;
    }

    T& value()
    {
        return value_;
    }
    const T& value() const
    {
        return value_;
    }
    T& get()
    {
        return value_;
    }

    const T& get() const
    {
        return value_;
    }

    T* operator->()
    {
        return &value_;
    }

    const T* operator->() const
    {
        return &value_;
    }

    T& operator*()
    {
        return value_;
    }

    const T& operator*() const
    {
        return value_;
    }

    const std::string& display_name() const
    {
        return display_name_;
    }

    const std::string& description() const
    {
        return description_;
    }

    const std::string& category() const
    {
        return category_;
    }

    bool editable() const
    {
        return editable_;
    }

    bool visible() const
    {
        return visible_;
    }

    template <typename Attr>
    std::optional<Attr> get_attribute() const
    {
        for ( const auto& attr : attributes_ )
        {
            if ( auto* ptr = std::get_if<Attr>( &attr ) )
            {
                return *ptr;
            }
        }
        return std::nullopt;
    }

    template <typename Attr>
    bool has_attribute() const
    {
        for ( const auto& attr : attributes_ )
        {
            if ( std::holds_alternative<Attr>( attr ) )
            {
                return true;
            }
        }
        return false;
    }

    const std::vector<AttributeVariant>& attributes() const
    {
        return attributes_;
    }

    auto reflection() const
    {
        MetadataMap metadata_vec;

        for ( const auto& attr : attributes_ )
        {
            std::string     key;
            MetadataVariant value;

            if ( auto* color_attr = std::get_if<ColorAttribute>( &attr ) )
            {
                key   = "color";
                value = *color_attr;
            }
            else if ( auto* direction_attr = std::get_if<DirectionAttribute>( &attr ) )
            {
                key   = "direction";
                value = *direction_attr;
            }
            else if ( auto* texture_attr = std::get_if<TextureAttribute>( &attr ) )
            {
                key   = "texture";
                value = *texture_attr;
            }
            else if ( auto* range_attr = std::get_if<RangeAttribute>( &attr ) )
            {
                key   = "range";
                value = *range_attr;
            }
            else
            {
                key   = "custom";
                value = attr;
            }

            if ( !key.empty() )
            {
                metadata_vec.emplace_back( key, value );
            }
        }

        return PropertiesImpl<_name, T>{ .value        = value_,
                                         .display_name = display_name_,
                                         .description  = description_,
                                         .category     = category_,
                                         .editable     = editable_,
                                         .visible      = visible_,
                                         .metadata     = metadata_vec };
    }
};

namespace rfl
{
    template <rfl::internal::StringLiteral _name, typename T>
    struct Reflector<Property<_name, T>>
    {
        using ReflType = decltype( std::declval<Property<_name, T>>().reflection() );

        // static inline Property<_name, T> to( const ReflType& impl ) noexcept
        //{
        //     Property<_name, T> prop;
        //     prop = impl.value();
        //     return prop;
        // }

        static inline ReflType from( const Property<_name, T>& prop )
        {
            return prop.reflection();
        }
    };
} // namespace rfl

#define FIELD( type, name, display_name )                                                                         \
    Property<#name, type> name = Property<#name, type>{ type{}, display_name };

#define FIELD_ATTR( type, name, display_name, ... )                                                               \
    Property<#name, type> name = Property<#name, type>{ type{}, display_name } __VA_ARGS__;

#define RFL_UB_TYPE( name, ... )                                                                                  \
    struct name                                                                                                   \
    {                                                                                                             \
        using value_type = name;                                                                                  \
        __VA_ARGS__                                                                                               \
                                                                                                                  \
        template <typename Archive>                                                                               \
        void serialize( Archive& archive ) const                                                                  \
        {                                                                                                         \
            rfl::to_named_tuple( *this ).apply( [&archive]( const auto&... fields )                               \
                                                { ( archive( fields.name(), fields.value() ), ... ); } );         \
        }                                                                                                         \
                                                                                                                  \
        template <typename Archive>                                                                               \
        void deserialize( Archive& archive )                                                                      \
        {                                                                                                         \
            auto named_tuple = rfl::to_named_tuple( *this );                                                      \
            named_tuple.apply( [&archive]( auto&... fields )                                                      \
                               { ( archive( fields.name(), fields.value() ), ... ); } );                          \
            *this = rfl::from_named_tuple<name>( named_tuple );                                                   \
        }                                                                                                         \
    };
