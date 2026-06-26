#include "ReflectionSerializer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Desert::Reflection
{
    namespace
    {
        // Accepts a JSON number stored either as integer or floating point.
        double AsNumber( const rfl::Generic& g )
        {
            if ( auto d = g.to_double(); d.has_value() )
                return d.value();
            if ( auto i = g.to_int64(); i.has_value() )
                return static_cast<double>( i.value() );
            return 0.0;
        }

        void WriteVec( rfl::Generic::Object& out, const std::string& name, const float* v, int count )
        {
            rfl::Generic::Array arr;
            for ( int i = 0; i < count; ++i )
                arr.push_back( rfl::Generic( static_cast<double>( v[i] ) ) );
            out[name] = std::move( arr );
        }

        void ReadVec( const rfl::Generic& g, float* v, int count )
        {
            auto arr = g.to_array();
            if ( !arr.has_value() )
                return;
            const auto& a = arr.value();
            for ( int i = 0; i < count && i < static_cast<int>( a.size() ); ++i )
                v[i] = static_cast<float>( AsNumber( a[i] ) );
        }

        // Enums can have any integral underlying type (enum class : uint8_t, etc.). Read/write exactly
        // `size` bytes so we never touch adjacent fields.
        int64_t ReadIntBySize( const void* p, std::size_t size )
        {
            switch ( size )
            {
                case 1:  return *static_cast<const int8_t*>( p );
                case 2:  return *static_cast<const int16_t*>( p );
                case 8:  return *static_cast<const int64_t*>( p );
                default: return *static_cast<const int32_t*>( p );
            }
        }

        void WriteIntBySize( void* p, std::size_t size, int64_t value )
        {
            switch ( size )
            {
                case 1:  *static_cast<int8_t*>( p )  = static_cast<int8_t>( value ); break;
                case 2:  *static_cast<int16_t*>( p ) = static_cast<int16_t>( value ); break;
                case 8:  *static_cast<int64_t*>( p ) = value; break;
                default: *static_cast<int32_t*>( p ) = static_cast<int32_t>( value ); break;
            }
        }
    } // namespace

    rfl::Generic::Object SerializeReflected( const TypeInfo& type, const void* obj, const AssetResolver* resolver )
    {
        rfl::Generic::Object out;
        const auto* base = static_cast<const std::byte*>( obj );

        for ( const auto& field : type.Fields )
        {
            const void* p = base + field.Offset;

            // Containers route through the codegen-emitted typed lambda (the switch can't iterate vectors).
            if ( field.IsContainer && field.SerializeContainer )
            {
                out[field.Name] = field.SerializeContainer( p );
                continue;
            }

            switch ( field.Type )
            {
                case FieldType::Bool:
                    out[field.Name] = *static_cast<const bool*>( p );
                    break;
                case FieldType::Int:
                    out[field.Name] = static_cast<int64_t>( *static_cast<const int32_t*>( p ) );
                    break;
                case FieldType::UInt:
                    out[field.Name] = static_cast<int64_t>( *static_cast<const uint32_t*>( p ) );
                    break;
                case FieldType::Float:
                    out[field.Name] = static_cast<double>( *static_cast<const float*>( p ) );
                    break;
                case FieldType::Double:
                    out[field.Name] = *static_cast<const double*>( p );
                    break;
                case FieldType::String:
                    out[field.Name] = *static_cast<const std::string*>( p );
                    break;
                case FieldType::Vec2:
                    WriteVec( out, field.Name, static_cast<const float*>( p ), 2 );
                    break;
                case FieldType::Vec3:
                    WriteVec( out, field.Name, static_cast<const float*>( p ), 3 );
                    break;
                case FieldType::Vec4:
                    WriteVec( out, field.Name, static_cast<const float*>( p ), 4 );
                    break;
                case FieldType::Enum:
                    out[field.Name] = ReadIntBySize( p, field.Size );
                    break;
                case FieldType::AssetHandle:
                    if ( resolver && resolver->ToPath )
                        out[field.Name] =
                             resolver->ToPath( *static_cast<const uint64_t*>( p ), field.Meta.AssetType );
                    else
                        out[field.Name] = static_cast<int64_t>( *static_cast<const uint64_t*>( p ) );
                    break;
                case FieldType::Struct:
                    if ( field.StructType )
                        out[field.Name] = SerializeReflected( *field.StructType, p, resolver );
                    break;
                default:
                    break;
            }
        }

        return out;
    }

    void DeserializeReflected( const TypeInfo& type, void* obj, const rfl::Generic::Object& src,
                               const AssetResolver* resolver )
    {
        auto* base = static_cast<std::byte*>( obj );

        for ( const auto& field : type.Fields )
        {
            auto found = src.get( field.Name );
            if ( !found.has_value() )
                continue; // missing key — keep the field's default value

            const rfl::Generic& g = found.value();
            void*               p = base + field.Offset;

            if ( field.IsContainer && field.DeserializeContainer )
            {
                field.DeserializeContainer( p, g );
                continue;
            }

            switch ( field.Type )
            {
                case FieldType::Bool:
                    if ( auto b = g.to_bool(); b.has_value() )
                        *static_cast<bool*>( p ) = b.value();
                    break;
                case FieldType::Int:
                    *static_cast<int32_t*>( p ) = static_cast<int32_t>( AsNumber( g ) );
                    break;
                case FieldType::UInt:
                    *static_cast<uint32_t*>( p ) = static_cast<uint32_t>( AsNumber( g ) );
                    break;
                case FieldType::Float:
                    *static_cast<float*>( p ) = static_cast<float>( AsNumber( g ) );
                    break;
                case FieldType::Double:
                    *static_cast<double*>( p ) = AsNumber( g );
                    break;
                case FieldType::String:
                    if ( auto s = g.to_string(); s.has_value() )
                        *static_cast<std::string*>( p ) = s.value();
                    break;
                case FieldType::Vec2:
                    ReadVec( g, static_cast<float*>( p ), 2 );
                    break;
                case FieldType::Vec3:
                    ReadVec( g, static_cast<float*>( p ), 3 );
                    break;
                case FieldType::Vec4:
                    ReadVec( g, static_cast<float*>( p ), 4 );
                    break;
                case FieldType::Enum:
                    WriteIntBySize( p, field.Size, static_cast<int64_t>( AsNumber( g ) ) );
                    break;
                case FieldType::AssetHandle:
                    if ( resolver && resolver->FromPath )
                    {
                        if ( auto s = g.to_string(); s.has_value() )
                            *static_cast<uint64_t*>( p ) = resolver->FromPath( s.value(), field.Meta.AssetType );
                        else // raw-handle fallback (e.g. files saved without a resolver)
                            *static_cast<uint64_t*>( p ) = static_cast<uint64_t>( AsNumber( g ) );
                    }
                    else
                    {
                        *static_cast<uint64_t*>( p ) = static_cast<uint64_t>( AsNumber( g ) );
                    }
                    break;
                case FieldType::Struct:
                    if ( field.StructType )
                    {
                        if ( auto o = g.to_object(); o.has_value() )
                            DeserializeReflected( *field.StructType, p, o.value(), resolver );
                    }
                    break;
                default:
                    break;
            }
        }
    }
} // namespace Desert::Reflection
