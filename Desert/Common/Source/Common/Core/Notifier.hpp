#pragma once

#include <functional>
#include <vector>

namespace Common
{
    class Notifier
    {
    public:
        using Callback   = std::function<void()>;
        using CallbackID = size_t;

        CallbackID AddCallback( Callback&& callback );

        void RemoveCallback( CallbackID id );

        void NotifyAll();

        void Clear();

    private:
        std::vector<Callback> m_Callbacks;
    };
} // namespace Common