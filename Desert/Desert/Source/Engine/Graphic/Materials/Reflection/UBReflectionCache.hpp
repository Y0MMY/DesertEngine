#pragma once
#include <rflcpp/rfl.hpp>
#include <vector>
#include <string_view>

namespace Desert::Graphic::Reflection
{
    struct UBFieldDescriptor
    {
        std::string_view name;

        void ( *read )( const void* object, void ( *callback )( std::string_view, const void* ) );
    };

    template <typename T>
    struct UBReflectionCache
    {
        using Descriptor = UBFieldDescriptor;

        static const std::vector<Descriptor>& Fields()
        {
            static const auto fields = Build();
            return fields;
        }

    private:
        static std::vector<Descriptor> Build()
        {
            std::vector<Descriptor> result;

            rfl::to_named_tuple( T{} ).apply(
                 [&]( auto&&... fields )
                 {
                     ( result.push_back(
                            Descriptor{ fields.name(),
                                        +[]( const void* obj, void ( *cb )( std::string_view, const void* ) )
                                        {
                                            const T&    typed = *static_cast<const T*>( obj );
                                            const auto& value = fields.get( typed );
                                            cb( fields.name(), &value );
                                        } } ),
                       ... );
                 } );

            return result;
        }
    };
} // namespace Desert::Graphic::Reflection
