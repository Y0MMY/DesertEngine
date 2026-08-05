#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Desert::UI
{
    // MVVM-lite: a flat key -> value store the UI binds to. Gameplay (C++ or Lua) writes "player.hp" and
    // every element bound to that key follows on the next frame — no element lookups, no per-widget glue,
    // and the writer never needs to know a UI exists.
    //
    // Deliberately global and flat: a UI binding is a name, and a scene has one screen. Values are copied
    // out on read, so a binding can never dangle.
    class UIDataStore
    {
    public:
        using Value = std::variant<double, bool, std::string, glm::vec3>;

        static UIDataStore& Get();

        void Set( const std::string& key, Value value );
        void Erase( const std::string& key );
        void Clear(); // scene change / play-stop: bindings must not survive into another world

        bool Has( const std::string& key ) const;

        // Typed reads. Each converts when it sensibly can (a number reads as text, "1"/"true" reads as a
        // bool) and returns nothing when it cannot, so a mistyped binding shows the authored value rather
        // than a garbage one.
        std::optional<double>      Number( const std::string& key ) const;
        std::optional<bool>        Bool( const std::string& key ) const;
        std::optional<std::string> Text( const std::string& key ) const;
        std::optional<glm::vec3>   Color( const std::string& key ) const;

        const std::unordered_map<std::string, Value>& All() const
        {
            return m_Values;
        }

    private:
        std::unordered_map<std::string, Value> m_Values;
    };

    // The other half of the bridge: messages the canvas raised this frame (button actions, pointer
    // events, drops) queued for gameplay to consume. ScriptSystem drains it and calls OnUIMessage on
    // every loaded script, so a button reaches Lua without the UI knowing scripting exists.
    class UIMessageQueue
    {
    public:
        static UIMessageQueue& Get();

        void Push( std::string message );
        // Takes everything queued and clears it — a message is delivered exactly once.
        std::vector<std::string> Drain();
        void                     Clear();

    private:
        std::vector<std::string> m_Messages;
    };
} // namespace Desert::UI
