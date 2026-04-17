//#pragma once
//
//#include <rflcpp/rfl.hpp>
//
//#include <array>
//#include <vector>
//#include <type_traits>
//#include <cstddef>
//
//// ------------------------------------------------------------
//// Shader buffer kind
//// ------------------------------------------------------------
//
//enum class ShaderBufferKind
//{
//    Uniform,
//    Storage
//};
//
//// ------------------------------------------------------------
//// Buffer tag types (compile-time dispatch)
//// ------------------------------------------------------------
//
//struct UniformBufferTag
//{
//};
//struct StorageBufferTag
//{
//};
//
//struct Texture2DTag
//{
//};
//struct TextureCubeTag
//{
//};
//
//#define FIELD( type, name ) type name = type{};
//
//// ------------------------------------------------------------
//// FIELD_array — type selector
//// ------------------------------------------------------------
//
//template <typename BufferTag, typename T, std::size_t N>
//struct field_array_selector;
//
//// Uniform → std::array
//template <typename T, std::size_t N>
//struct field_array_selector<UniformBufferTag, T, N>
//{
//    using type = std::array<T, N>;
//};
//
//// SSBO → std::vector
//template <typename T, std::size_t N>
//struct field_array_selector<StorageBufferTag, T, N>
//{
//    using type = std::vector<T>;
//};
//
//template <typename Tag>
//struct TextureBinding;
//
//template <>
//struct TextureBinding<Texture2DTag>
//{
//    using value_type = Desert::Graphic::Image2DRef;
//};
//
//template <>
//struct TextureBinding<TextureCubeTag>
//{
//    using value_type = Desert::Graphic::ImageCubeRef;
//};
//
//// ------------------------------------------------------------
//// FIELD_array macro
//// usage: FIELD_array( float, viewProj, [16] )
//// ------------------------------------------------------------
//
//#define FIELD_ARRAY( type_, name, arr )                                                                           \
//    typename field_array_selector<buffer_tag, type_, std::extent_v<type_ arr>>::type name{};
//
//#define TEXTURE_2D( name ) TextureBinding<Texture2DTag> name{};
//#define TEXTURE_CUBE( name ) TextureBinding<TextureCubeTag> name{};
//
//// ------------------------------------------------------------
//// Public macros
//// ------------------------------------------------------------
//
//#define TEXTURE_BINDINGS( ... )                                                                                   \
//    struct Textures                                                                                               \
//    {                                                                                                             \
//        __VA_ARGS__                                                                                               \
//    };
//
//#define UNIFORM_BUFFER_STRUCT( Name, ... )                                                                        \
//    struct Name                                                                                                   \
//    {                                                                                                             \
//        inline static constexpr const char*      shader_buffer_name = #Name;                                      \
//        inline static constexpr ShaderBufferKind buffer_kind        = ShaderBufferKind::Uniform;                  \
//        using buffer_tag                                            = UniformBufferTag;                           \
//        using value_type                                            = Name;                                       \
//                                                                                                                  \
//        __VA_ARGS__                                                                                               \
//    };
//
//#define SSBO_STRUCT( Name, ... )                                                                                  \
//    struct Name                                                                                                   \
//    {                                                                                                             \
//        inline static constexpr const char*      shader_buffer_name = #Name;                                      \
//        inline static constexpr ShaderBufferKind buffer_kind        = ShaderBufferKind::Storage;                  \
//        using buffer_tag                                            = StorageBufferTag;                           \
//        using value_type                                            = Name;                                       \
//                                                                                                                  \
//        __VA_ARGS__                                                                                               \
//                                                                                                                  \
//        template <typename Func>                                                                                  \
//        void for_each_field_read( Func&& func ) const                                                             \
//        {                                                                                                         \
//            auto fields = rfl::to_named_tuple( *this );                                                           \
//            fields.apply( [&]( auto&&... field ) { ( func( field.name(), field.value() ), ... ); } );             \
//        }                                                                                                         \
//                                                                                                                  \
//        template <typename Func>                                                                                  \
//        void for_each_field( Func&& func )                                                                        \
//        {                                                                                                         \
//            auto fields = rfl::to_named_tuple( *this );                                                           \
//            fields.apply( [&]( auto&&... field ) { ( func( field.name(), field.value() ), ... ); } );             \
//        }                                                                                                         \
//    };
