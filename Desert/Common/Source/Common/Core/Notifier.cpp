#include "Notifier.hpp"

namespace Common
{
    Notifier::CallbackID Notifier::AddCallback( Callback&& callback )
    {
        m_Callbacks.push_back( std::move( callback ) );
        return m_Callbacks.size() - 1;
    }

    void Notifier::RemoveCallback( CallbackID id )
    {
        if ( id < m_Callbacks.size() )
        {
            m_Callbacks[id] = nullptr;
        }
    }

    void Notifier::NotifyAll()
    {
        for ( const auto& callback : m_Callbacks )
        {
            if ( callback )
            {
                callback();
            }
        }
    }

    void Notifier::Clear()
    {
        m_Callbacks.clear();
    }
} // namespace Common
