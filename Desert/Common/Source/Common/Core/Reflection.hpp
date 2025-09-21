#pragma once

#include "TemplateHelpers.hpp"

template <typename T, typename = void>
struct is_reflected : std::false_type
{
};

template <typename T>
struct is_reflected<T, std::void_t<decltype( std::declval<T>().update_buffer() )>> : std::true_type
{
};

#define RFL_UB_TYPE( name, ... )                                                                                  \
    struct name##Data                                                                                             \
    {                                                                                                             \
                                                                                                                  \
        using value_type = name##Data;                                                                            \
        __VA_ARGS__                                                                                               \
                                                                                                                  \
        template <typename Archive>                                                                               \
        void serialize( Archive& archive ) const                                                                  \
        {                                                                                                         \
            rfl::to_named_tuple( *this ).apply(                                                                   \
                 [&archive]( const auto&... fields )                                                              \
                 {                                                                                                \
                     ( archive( rfl::internal::StringLiteral<fields.name##Data().size()>(                         \
                                     fields.name##Data().data() ),                                                \
                                fields.value() ),                                                                 \
                       ... );                                                                                     \
                 } );                                                                                             \
        }                                                                                                         \
                                                                                                                  \
        template <typename Archive>                                                                               \
        void deserialize( Archive& archive )                                                                      \
        {                                                                                                         \
            auto named_tuple = rfl::to_named_tuple( *this );                                                      \
            named_tuple.apply(                                                                                    \
                 [&archive]( auto&... fields )                                                                    \
                 {                                                                                                \
                     ( archive( rfl::internal::StringLiteral<fields.name##Data().size()>(                         \
                                     fields.name##Data().data() ),                                                \
                                fields.value() ),                                                                 \
                       ... );                                                                                     \
                 } );                                                                                             \
            *this = rfl::from_named_tuple<std::decay_t<decltype( *this )>>( named_tuple );                        \
        }                                                                                                         \
    };                                                                                                            \
                                                                                                                  \
    struct name : public name##Data                                                                               \
    {                                                                                                             \
    private:                                                                                                      \
        mutable std::vector<std::byte> data_buffer_;                                                              \
        mutable bool                   buffer_dirty_ = true;                                                      \
                                                                                                                  \
    public:                                                                                                       \
        using value_type = name;                                                                                  \
        using name##Data::name##Data;                                                                             \
                                                                                                                  \
        name( const name& other ) : name##Data( other )                                                           \
        {                                                                                                         \
            buffer_dirty_ = true;                                                                                 \
        }                                                                                                         \
                                                                                                                  \
        void update_buffer() const                                                                                \
        {                                                                                                         \
            if ( !buffer_dirty_ )                                                                                 \
                return;                                                                                           \
            data_buffer_.clear();                                                                                 \
            const auto named_tuple = rfl::to_named_tuple( static_cast<const name##Data&>( *this ) );              \
            named_tuple.apply(                                                                                    \
                 [this]( const auto& field )                                                                      \
                 {                                                                                                \
                     using FieldType = std::decay_t<decltype( field )>;                                           \
                     using ValueType = typename FieldType::Type;                                                  \
                                                                                                                  \
                     if constexpr ( is_container<ValueType>::value )                                              \
                     {                                                                                            \
                         const auto& container = field.value();                                                   \
                         if ( !container.empty() )                                                                \
                         {                                                                                        \
                             const std::byte* data_ptr = reinterpret_cast<const std::byte*>( container.data() );  \
                             size_t data_size = sizeof( typename ValueType::value_type ) * container.size();      \
                             data_buffer_.insert( data_buffer_.end(), data_ptr, data_ptr + data_size );           \
                         }                                                                                        \
                     }                                                                                            \
                     else                                                                                         \
                     {                                                                                            \
                         const std::byte* data_ptr  = reinterpret_cast<const std::byte*>( &field.value() );       \
                         size_t           data_size = sizeof( ValueType );                                        \
                         data_buffer_.insert( data_buffer_.end(), data_ptr, data_ptr + data_size );               \
                     }                                                                                            \
                 } );                                                                                             \
            buffer_dirty_ = false;                                                                                \
        }                                                                                                         \
                                                                                                                  \
        const void* data() const noexcept                                                                         \
        {                                                                                                         \
            update_buffer();                                                                                      \
            return data_buffer_.data();                                                                           \
        }                                                                                                         \
                                                                                                                  \
        void* data() noexcept                                                                                     \
        {                                                                                                         \
            buffer_dirty_ = true;                                                                                 \
            update_buffer();                                                                                      \
            return data_buffer_.data();                                                                           \
        }                                                                                                         \
                                                                                                                  \
        size_t size() const noexcept                                                                              \
        {                                                                                                         \
            update_buffer();                                                                                      \
            return data_buffer_.size();                                                                           \
        }                                                                                                         \
    };
#define FIELD( name, type ) rfl::Field<#name, type> name = type{};