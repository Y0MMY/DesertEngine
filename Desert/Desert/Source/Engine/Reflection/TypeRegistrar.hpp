#pragma once

#include <Engine/Reflection/ReflectionRegistry.hpp>

#include <utility>

namespace Desert::Reflection
{
    // Fluent helper used by generated reflection code to assemble and register a TypeInfo.
    //
    //   TypeBuilder( "PBRMaterialData", sizeof( PBRMaterialData ) )
    //       .Field( { .Name = "AlbedoColor", .Type = FieldType::Vec4,
    //                 .Offset = offsetof( PBRMaterialData, AlbedoColor ),
    //                 .Size = sizeof( glm::vec4 ), .Meta = { .Category = "Surface", .IsColor = true } } )
    //       .Register();
    class TypeBuilder
    {
    public:
        TypeBuilder( std::string name, std::size_t size )
        {
            m_Info.Name = std::move( name );
            m_Info.Size = size;
        }

        TypeBuilder& Field( FieldInfo field )
        {
            m_Info.Fields.push_back( std::move( field ) );
            return *this;
        }

        const TypeInfo* Register()
        {
            return ReflectionRegistry::Get().Register( std::move( m_Info ) );
        }

    private:
        TypeInfo m_Info;
    };
} // namespace Desert::Reflection
