#pragma once

namespace Common::Memory
{
    template <typename T>
    class not_null
    {
    public:
        not_null( T* ptr ) : m_Ptr( ptr )
        {
            DESERT_VERIFY( ptr != nullptr );
        }
        not_null( T& ref ) : m_Ptr( &ref )
        {
        }

        ~not_null()
        {
            m_Ptr = nullptr;
        }

        not_null( const not_null& )            = default;
        not_null& operator=( const not_null& ) = default;
        not_null( std::nullptr_t )             = delete;
        not_null( not_null&& other ) noexcept : m_Ptr( other.m_Ptr )
        {
        }
        not_null& operator=( not_null&& other ) noexcept
        {
            m_Ptr = other.m_Ptr;
            return *this;
        }

        T* get() const
        {
            return m_Ptr;
        }
        T* operator->() const
        {
            return m_Ptr;
        }

    private:
        T* m_Ptr;
    };
} // namespace Common::Memory
