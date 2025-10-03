#pragma once

#include <rflcpp/rfl.hpp>
#include "FieldWithProperty.hpp"

#define RFL_UB_TYPE( name, ... )                                                                                  \
    struct name                                                                                                   \
    {                                                                                                             \
        using value_type = name;                                                                                  \
        __VA_ARGS__                                                                                               \
                                                                                                                  \
        template <typename Visitor>                                                                               \
        void visit_fields( Visitor&& visitor ) const                                                              \
        {                                                                                                         \
            const auto named_tuple = rfl::to_named_tuple( *this );                                                \
            named_tuple.apply( [&visitor]( const auto&... fields )                                                \
                               { ( visitor( fields.name(), fields.value() ), ... ); } );                          \
        }                                                                                                         \
                                                                                                                  \
        template <typename Visitor>                                                                               \
        void visit_fields( Visitor&& visitor )                                                                    \
        {                                                                                                         \
            auto named_tuple = rfl::to_named_tuple( *this );                                                      \
            named_tuple.apply( [&visitor]( auto&... fields )                                                      \
                               { ( visitor( fields.name(), fields.value() ), ... ); } );                          \
        }                                                                                                         \
                                                                                                                  \
        template <typename Archive>                                                                               \
        void serialize( Archive& archive ) const                                                                  \
        {                                                                                                         \
            rfl::to_named_tuple( *this ).apply( [&archive]( const auto&... fields )                               \
                                                { ( archive( fields.name(), fields.value().Value ), ... ); } );   \
        }                                                                                                         \
                                                                                                                  \
        template <typename Archive>                                                                               \
        void deserialize( Archive& archive )                                                                      \
        {                                                                                                         \
            auto named_tuple = rfl::to_named_tuple( *this );                                                      \
            named_tuple.apply( [&archive]( auto&... fields )                                                      \
                               { ( archive( fields.name(), fields.value().Value ), ... ); } );                    \
            *this = rfl::from_named_tuple<name>( named_tuple );                                                   \
        }                                                                                                         \
    };

#define FIELD_COLOR3( name, display_name ) rfl::Field<#name, ColorProperty> name = glm::vec3( 1.0f );
#define FIELD_VALUEF( name, display_name ) rfl::Field<#name, ValuePropertyF> name = 0.0f;
#define FIELD_VALUEUINT( name, display_name ) rfl::Field<#name, ValuePropertyUINT> name = 0U;
#define FIELD_MAT4( name, display_name ) rfl::Field<#name, ValuePropertyM4> name = glm::mat4( 0.0f );
#define FIELD_POSITION( name, display_name ) rfl::Field<#name, ValuePropertyPosition> name = glm::vec3( 1.0f );

#define FIELD( data_type, name, display_name ) rfl::Field<#name, data_type> name = data_type{};