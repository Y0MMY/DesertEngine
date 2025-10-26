#pragma once

template <typename T>
struct is_container : std::false_type
{
};

template <typename T, typename Alloc>
struct is_container<std::vector<T, Alloc>> : std::true_type
{
};

template <typename T, std::size_t N>
struct is_container<std::array<T, N>> : std::true_type
{
};

template <typename T, typename Alloc>
struct is_container<std::list<T, Alloc>> : std::true_type
{
};

template <typename T>
struct is_optional : std::false_type
{
};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type
{
};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;